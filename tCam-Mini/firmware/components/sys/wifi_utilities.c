/*
 * WiFi related utilities
 *
 * Contains functions to initialize and query the wifi interface.  Also includes
 * the system event handler for use by the wifi system.
 *
 * Note: Currently only 1 station is allowed to connect at a time.
 *
 * Copyright 2020-2022 Dan Julio
 *
 * This file is part of tCam.
 *
 * tCam is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * tCam is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with tCam.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "wifi_utilities.h"
#include "ps_utilities.h"
#include "dns_hijack.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "nvs_flash.h"
#include <string.h>



//
// Wifi Utilities local variables
//
static const char* TAG = "wifi_utilities";

// Wifi netif instance (changed each time wifi is re-started)
static esp_netif_t *wifi_netif;

// Wifi information
static char wifi_ap_ssid_array[PS_SSID_MAX_LEN+1];
static char wifi_sta_ssid_array[PS_SSID_MAX_LEN+1];
static char wifi_ap_pw_array[PS_PW_MAX_LEN+1];
static char wifi_sta_pw_array[PS_PW_MAX_LEN+1];
static net_info_t wifi_info = {
	wifi_ap_ssid_array,
	wifi_sta_ssid_array,
	wifi_ap_pw_array,
	wifi_sta_pw_array,
	0,
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0}
};

static const wifi_country_t def_country_info = {
	"US",
	1,
	11,
	20,
	WIFI_COUNTRY_POLICY_AUTO
};


static bool sta_connected = false; // Set when we connect to an AP so we can disconnect if we restart
static int sta_retry_num = 0;

// Deferred station reconnect.  Retrying from inside the disconnect event used a
// blocking delay in the system event handler - stalling AP associations, DHCP
// and every other event behind it - and kept the radio off-channel near
// continuously once the recovery AP was up.  A one-shot timer costs neither.
static esp_timer_handle_t reconnect_timer = NULL;

// While true a scan owns the radio: the disconnect handler must not schedule a
// reconnect and the reconnect timer must not fire a connect underneath it
static volatile bool scan_hold = false;
static int sta_fail_count = 0;     // Consecutive failed attempts, for fallback triggering

// Recovery access point state.  When the camera is configured to join a network
// it cannot find - typically because it moved to a new location - it raises its
// own access point alongside the (still retrying) station so it can be
// reconfigured from the captive portal without a reset or a cable.
static bool fallback_active = false;
static esp_netif_t *fallback_netif = NULL;



//
// WiFi Utilities Forward Declarations for internal functions
//
static bool init_esp_wifi();
static bool enable_esp_wifi_ap();
static bool enable_esp_wifi_client();
static bool sta_should_connect();
static bool apply_ap_ip_config(esp_netif_t* netif);
static void load_ap_wifi_config(wifi_config_t* cfg);
static void enable_fallback_ap();
static void disable_fallback_ap();
static void schedule_reconnect(uint32_t delay_ms);
static void reconnect_timer_cb(void* arg);
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);



//
// WiFi Utilities API
//

/**
 * Power-on initialization of the WiFi system.  It is enabled based on start-up
 * information from persistent storage.  Returns false if any part of the initialization
 * fails.
 */
bool wifi_init()
{
	esp_err_t ret;
	
	// Initialize the TCP/IP stack
	ret = esp_netif_init();
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Could not init netif (%d)", ret);
		return false;
	}
	
	// Setup the default event handlers
	ret = esp_event_loop_create_default();
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Could not create default event loop handler (%d)", ret);
		return false;
	}
	
	ret = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Could not register wifi_event_handler (%d)", ret);
		return false;
	}
	
	// STA_GOT_IP, not ETH_GOT_IP: this module only ever runs the WiFi interface
	// (ethernet has its own utilities), and the DHCP address arrives with the
	// station event.  The handler was registered for the ethernet event, so in
	// station mode it never fired and cur_ip_addr stayed 0.0.0.0.
	ret = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Could not register ip_event_handler (%d)", ret);
		return false;
	}
	
	// Initialize NVS
	ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ret = nvs_flash_erase();
		if (ret != ESP_OK) {
			ESP_LOGI(TAG, "nvs_flash_erase failed (%d)", ret);
			return false;
		}
		ret = nvs_flash_init();
	}
	if (ret != ESP_OK) {
		ESP_LOGI(TAG, "nvs_flash_init failed (%d)", ret);
		return false;
	}
	
	// Get our wifi info
	ps_get_net_info(&wifi_info);
	
	// Initialize the WiFi interface
	if (init_esp_wifi()) {
		wifi_info.flags |= NET_INFO_FLAG_INITIALIZED;
		ESP_LOGI(TAG, "WiFi initialized");
		
		// Configure the WiFi interface if enabled
		if ((wifi_info.flags & NET_INFO_FLAG_STARTUP_ENABLE) != 0) {
			if ((wifi_info.flags & NET_INFO_FLAG_CLIENT_MODE) != 0) {
				if (enable_esp_wifi_client()) {
					wifi_info.flags |= NET_INFO_FLAG_ENABLED;
					ESP_LOGI(TAG, "WiFi Station starting");
				} else {
					return false;
				}
			} else {
				if (enable_esp_wifi_ap()) {
					wifi_info.flags |= NET_INFO_FLAG_ENABLED;
					ESP_LOGI(TAG, "WiFi AP %s enabled", wifi_info.ap_ssid);
				} else {
					return false;
				}
			}
		}
	} else {
		ESP_LOGE(TAG, "WiFi Initialization failed");
		return false;
	}
	
	return true;
}


/**
 * Re-initialize the WiFi system when information such as the SSID, password or enable-
 * state have changed.  Returns false if anything fails.
 */
bool wifi_reinit()
{
	// Cancel any deferred reconnect - it belongs to the configuration being
	// discarded - and release a scan hold left by an interrupted scan
	if (reconnect_timer != NULL) {
		esp_timer_stop(reconnect_timer);
	}
	scan_hold = false;

	// Tear down a recovery AP before rebuilding - the netif is destroyed here so
	// the next fallback episode (if any) creates it fresh against the new config
	if (fallback_active) {
		dns_hijack_stop();
		fallback_active = false;
	}
	if (fallback_netif != NULL) {
		esp_netif_destroy_default_wifi(fallback_netif);
		fallback_netif = NULL;
	}
	sta_fail_count = 0;

	// Attempt to disconnect from an AP if we were previously connected
	if (sta_connected) {
		ESP_LOGI(TAG, "Attempting to disconnect from AP");
		esp_wifi_disconnect();
		sta_connected = false;
	}
	
	// Shut down the old configuration
	if ((wifi_info.flags & NET_INFO_FLAG_ENABLED) != 0) {
		ESP_LOGI(TAG, "WiFi stopping");
		esp_wifi_stop();
		wifi_info.flags &= ~NET_INFO_FLAG_ENABLED;
	}
	
	// Destroy the associated esp_netif object
	esp_netif_destroy_default_wifi(wifi_netif);
	wifi_netif = NULL;

	if ((wifi_info.flags & NET_INFO_FLAG_INITIALIZED) == 0) {
		// Attempt to initialize the wifi interface again
		if (!init_esp_wifi()) {
			return false;
		}
	}
	
	// Update the wifi info because we're called when it's updated
	ps_get_net_info(&wifi_info);
	wifi_info.flags |= NET_INFO_FLAG_INITIALIZED;   // Add in the fact we're already initialized
	
	// Reconfigure the interface if enabled
	if ((wifi_info.flags & NET_INFO_FLAG_STARTUP_ENABLE) != 0) {
		if ((wifi_info.flags & NET_INFO_FLAG_CLIENT_MODE) != 0) {
			if (enable_esp_wifi_client()) {
				wifi_info.flags |= NET_INFO_FLAG_ENABLED;
				ESP_LOGI(TAG, "WiFi Station starting");
			} else {
				return false;
			}
		} else {
			if (enable_esp_wifi_ap()) {
				wifi_info.flags |= NET_INFO_FLAG_ENABLED;
				ESP_LOGI(TAG, "WiFi AP %s enabled", wifi_info.ap_ssid);
			} else {
				return false;
			}
		}
	}
	
	// Nothing should be connected now
	wifi_info.flags &= ~NET_INFO_FLAG_CONNECTED;
	
	return true;
}


/**
 * Return connected to client status
 */
bool wifi_is_connected()
{
	return ((wifi_info.flags & NET_INFO_FLAG_CONNECTED) != 0);
}


bool wifi_is_ap_mode()
{
	return ((wifi_info.flags & NET_INFO_FLAG_CLIENT_MODE) == 0);
}


/**
 * True when the station interface is meant to associate with an access point.
 * False when it is only running so the web UI can scan.
 */
static bool sta_should_connect()
{
	if ((wifi_info.flags & NET_INFO_FLAG_CLIENT_MODE) == 0) return false;

	return (strlen(wifi_info.sta_ssid) != 0);
}


/**
 * Return current WiFi configuration and state
 */
net_info_t* wifi_get_info()
{
	return &wifi_info;
}


void wifi_scan_prepare()
{
	scan_hold = true;

	if (reconnect_timer != NULL) {
		esp_timer_stop(reconnect_timer);
	}

	// Abort a connect attempt in flight - the driver refuses to scan during one,
	// and with an unreachable network configured there is nearly always one in
	// flight.  The disconnect event this triggers sees scan_hold and stays quiet.
	// A station that is actually associated is left alone; scanning is legal then.
	// sta_connected, not NET_INFO_FLAG_CONNECTED - the flag describes the AP while
	// the fallback is up, and this decision is about the station.
	if (sta_should_connect() && !sta_connected) {
		esp_wifi_disconnect();
	}
}


void wifi_scan_complete()
{
	scan_hold = false;

	// Resume the pursuit of the configured network at the calm cadence - the
	// operator is clearly present and using the interface right now
	if (sta_should_connect() && !sta_connected) {
		schedule_reconnect(fallback_active ? WIFI_FALLBACK_RETRY_MSEC : 1000);
	}
}



//
// WiFi Utilities internal functions
//

/**
 * Initialize the WiFi interface resources
 */
static bool init_esp_wifi()
{
	esp_err_t ret;
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	
	ret = esp_wifi_init(&cfg);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Could not allocate wifi resources (%d)", ret);
		return false;
	}
	
	// We don't need the NVS configuration storage for the WiFi configuration since we
	// are managing persistent storage ourselves
	ret = esp_wifi_set_storage(WIFI_STORAGE_RAM);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Could not set RAM storage for configuration (%d)", ret);
		return false;
	}
		
	// Setup WiFi country restrictions to US/AUTO
	ret = esp_wifi_set_country(&def_country_info);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Could not set default country configuration - %x", ret);
		return false;
	}
	
	return true;
}


/**
 * Enable this device as a Soft AP
 */
static bool enable_esp_wifi_ap()
{
	esp_err_t ret;
	int i;
	
	// Create the esp_netif object
	wifi_netif = esp_netif_create_default_wifi_ap();

	if (!apply_ap_ip_config(wifi_netif)) {
		return false;
	}

	// Enable the AP
	wifi_config_t wifi_config;
	load_ap_wifi_config(&wifi_config);

    // APSTA rather than AP: the station interface is not associated with anything,
    // but its presence is what allows esp_wifi_scan_start() to run while we are
    // serving the SoftAP.  That is what lets the web UI show the user a list of
    // nearby networks to join instead of making them type an SSID blind.
    ret = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (ret != ESP_OK) {
    	ESP_LOGE(TAG, "Could not set Soft AP mode (%d)", ret);
    	return false;
    }
    
    ret = esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config);
    if (ret != ESP_OK) {
    	ESP_LOGE(TAG, "Could not set Soft AP configuration (%d)", ret);
    	return false;
    }
    
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
    	ESP_LOGE(TAG, "Could not start Soft AP (%d)", ret);
    	return false;
    }
    
    // For now, since we are using the default IP address, copy it to the current here
    for (i=0; i<4; i++) {
    	wifi_info.cur_ip_addr[i] = wifi_info.ap_ip_addr[i];
    }
    	
    return true;
}


/**
 * Enable this device as a Client
 */
static bool enable_esp_wifi_client()
{
	esp_err_t ret;
	esp_netif_ip_info_t ipInfo;
	
	
	// Configure the IP address mechanism
	if ((wifi_info.flags & NET_INFO_FLAG_CL_STATIC_IP) != 0) {
		// Static IP
		//
		// Create the esp_netif object
		wifi_netif = esp_netif_create_default_wifi_sta();
		
		// Stop the DHCP client
		ret = esp_netif_dhcpc_stop(wifi_netif);
		if ((ret != ESP_OK) && (ret != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED)) {
    		ESP_LOGE(TAG, "Stop Station DHCP returned %d", ret);
    		return false;
    	}
    	
    	// Set the Static IP address
		ipInfo.ip.addr = wifi_info.sta_ip_addr[3] |
						 (wifi_info.sta_ip_addr[2] << 8) |
						 (wifi_info.sta_ip_addr[1] << 16) |
						 (wifi_info.sta_ip_addr[0] << 24);
		ipInfo.gw.addr = esp_netif_ip4_makeu32(0, 0, 0, 0);
  		ipInfo.netmask.addr = wifi_info.sta_netmask[3] |
						     (wifi_info.sta_netmask[2] << 8) |
						     (wifi_info.sta_netmask[1] << 16) |
						     (wifi_info.sta_netmask[0] << 24);
		ret = esp_netif_set_ip_info(wifi_netif, &ipInfo);
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "Set IP info returned %d", ret);
		}
	} else {
		// DHCP served address
		//
		// Create the esp_netif object
		wifi_netif = esp_netif_create_default_wifi_sta();
		
		ret = esp_netif_dhcpc_start(wifi_netif);
		if (ret != ESP_OK) {
    		ESP_LOGE(TAG, "Start Station DHCP returned %d", ret);
    		return false;
    	}
	}
	
	// Enable the Client
	wifi_config_t wifi_config = {
		.sta = {
			.scan_method = WIFI_FAST_SCAN,
			.bssid_set = 0,
			.channel = 0,
			.listen_interval = 0,
			.sort_method = WIFI_CONNECT_AP_BY_SIGNAL			
		}
	};	
    strcpy((char*) wifi_config.sta.ssid, wifi_info.sta_ssid);
    if (strlen(wifi_info.sta_pw) == 0) {
        strcpy((char*) wifi_config.sta.password, "");
    } else {
    	strcpy((char*) wifi_config.sta.password, wifi_info.sta_pw);
    }
    
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
    	ESP_LOGE(TAG, "Could not set Station mode (%d)", ret);
    	return false;
    }
    
    ret = esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
    if (ret != ESP_OK) {
    	ESP_LOGE(TAG, "Could not set Station configuration (%d)", ret);
    	return false;
    }
    
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
    	ESP_LOGE(TAG, "Could not start Station (%d)", ret);
    	return false;
    }
    
    return true;
}


/*
 * Handle events from the WiFi stack
 */

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
	wifi_event_ap_staconnected_t *con_event;
	wifi_event_ap_stadisconnected_t *dis_event;
	
	switch (event_id) {
		case WIFI_EVENT_AP_STACONNECTED:
			con_event = (wifi_event_ap_staconnected_t *) event_data;
			wifi_info.flags |= NET_INFO_FLAG_CONNECTED;
			ESP_LOGI(TAG, "Station:"MACSTR" join, AID=%d", MAC2STR(con_event->mac), con_event->aid);
			break;
		
		case WIFI_EVENT_AP_STADISCONNECTED:
			dis_event = (wifi_event_ap_stadisconnected_t *) event_data;
			wifi_info.flags &= ~NET_INFO_FLAG_CONNECTED;
			ESP_LOGI(TAG, "Station:"MACSTR" leave, AID=%d", MAC2STR(dis_event->mac), dis_event->aid);
			break;
			
		case WIFI_EVENT_STA_START:
			// While we are an access point the station interface exists only so the
			// web UI can scan for networks - there is nothing to associate with.
			// Calling esp_wifi_connect() with an empty SSID just fails, and any
			// resulting disconnect event would start a pointless retry loop.
			if (!sta_should_connect()) {
				ESP_LOGI(TAG, "Station started (scan only)");
				break;
			}
			ESP_LOGI(TAG, "Station started, trying to connect to %s", wifi_info.sta_ssid);
			esp_wifi_connect();
			sta_retry_num = 0;
        	break;
        
        case WIFI_EVENT_STA_STOP:
        	ESP_LOGI(TAG, "Station stopped");
        	break;
        	
        case WIFI_EVENT_STA_CONNECTED:
        	ESP_LOGI(TAG, "Station connected");
        	if ((wifi_info.flags & NET_INFO_FLAG_CLIENT_MODE) != 0) {
        		// Client mode connect happens here (AP mode connect happens when we get an IP address)
        		wifi_info.flags |= NET_INFO_FLAG_CONNECTED;
        	}
        	break;
        	
        case WIFI_EVENT_STA_DISCONNECTED:
        	// In fallback the AP is our functioning interface - do not clear the
        	// flag the web server and portal depend on just because the station
        	// side of the radio failed another attempt
        	if (!fallback_active) {
        		wifi_info.flags &= ~NET_INFO_FLAG_CONNECTED;
        	}
        	// The station itself is genuinely down either way.  NET_INFO_FLAG_CONNECTED
        	// is overloaded in fallback (it describes the AP), so everything that needs
        	// the truth about the station - scan bracketing, the reconnect timer -
        	// reads sta_connected instead.
        	sta_connected = false;
        	// A scan-only station has nothing to reconnect to.  Retrying here would
        	// spin the driver through connect/fail/connect as fast as it can, which
        	// disturbs the access point we are actually serving.
        	if (!sta_should_connect()) {
        		break;
        	}
        	// A scan owns the radio right now; wifi_scan_complete() re-arms
        	if (scan_hold) {
        		break;
        	}
        	// The configured network has been unreachable long enough that the
        	// camera has probably moved.  Raise the recovery AP so it can be
        	// re-provisioned from the captive portal; the station keeps retrying
        	// underneath, so a returning network is rejoined automatically.
        	if (++sta_fail_count == WIFI_FALLBACK_AP_FAILS) {
        		enable_fallback_ap();
        	}
        	// Never block in this handler - it runs on the system event task, and a
        	// delay here stalls AP associations and DHCP behind it.  Fast attempts
        	// right after losing a link, then a timer takes over; once the recovery
        	// AP is up, long intervals so the AP being served stays usable.
        	if (fallback_active) {
        		schedule_reconnect(WIFI_FALLBACK_RETRY_MSEC);
        	} else if (sta_retry_num > WIFI_FAST_RECONNECT_ATTEMPTS) {
        		schedule_reconnect(1000);
        	} else {
        		sta_retry_num++;
        		esp_wifi_connect();
        		ESP_LOGI(TAG, "Retry connection to %s", wifi_info.sta_ssid);
        	}
        	break;
	}
}


/**
 * Arm (or re-arm) the one-shot reconnect timer.  Created lazily so every path -
 * first boot, reinit, fallback - shares the same instance.
 */
static void schedule_reconnect(uint32_t delay_ms)
{
	if (reconnect_timer == NULL) {
		const esp_timer_create_args_t args = {
			.callback = reconnect_timer_cb,
			.name = "wifi_reconnect"
		};

		if (esp_timer_create(&args, &reconnect_timer) != ESP_OK) {
			ESP_LOGE(TAG, "Could not create reconnect timer - retrying inline");
			esp_wifi_connect();
			return;
		}
	}

	esp_timer_stop(reconnect_timer);
	esp_timer_start_once(reconnect_timer, (uint64_t) delay_ms * 1000);
}


static void reconnect_timer_cb(void* arg)
{
	(void) arg;

	// The world may have changed while the timer ran: a scan may own the radio,
	// the network may have been joined, or the configuration replaced
	if (scan_hold) return;
	if (!sta_should_connect()) return;
	if (sta_connected) return;

	esp_wifi_connect();
	ESP_LOGI(TAG, "Retry connection to %s", wifi_info.sta_ssid);
}


/*
 * Handle events from the TCP/IP stack
 */

static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
	ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
	const esp_netif_ip_info_t *ip_info = &event->ip_info;

	// The configured network is reachable again (or the camera was just
	// re-provisioned onto a new one) - the recovery AP has done its job
	disable_fallback_ap();

	// A reconnect scheduled before this success would fire a pointless connect
	if (reconnect_timer != NULL) {
		esp_timer_stop(reconnect_timer);
	}

	wifi_info.flags |= NET_INFO_FLAG_CONNECTED;
    sta_connected = true;
    sta_retry_num = 0;
    sta_fail_count = 0;
    
	ESP_LOGI(TAG, "Got IP Address: " IPSTR, IP2STR(&ip_info->ip));
	
    wifi_info.cur_ip_addr[3] = ip_info->ip.addr & 0xFF;
    wifi_info.cur_ip_addr[2] = (ip_info->ip.addr >> 8) & 0xFF;
    wifi_info.cur_ip_addr[1] = (ip_info->ip.addr >> 16) & 0xFF;
	wifi_info.cur_ip_addr[0] = (ip_info->ip.addr >> 24) & 0xFF;
}


/**
 * Push the configured AP address into a netif and restart its DHCP server.
 * Persistent storage always carried an AP address but nothing ever applied it,
 * so the camera used to come up on the esp_netif default of 192.168.4.1.  The
 * DHCP server must be stopped around the change - the stop fails harmlessly if
 * it has not started yet - and the DHCP pool follows the address automatically.
 */
static bool apply_ap_ip_config(esp_netif_t* netif)
{
	esp_err_t ret;
	esp_netif_ip_info_t ip_info;

	// Note: index 3 holds the first octet (see ps_utilities)
	esp_netif_set_ip4_addr(&ip_info.ip, wifi_info.ap_ip_addr[3], wifi_info.ap_ip_addr[2],
	                       wifi_info.ap_ip_addr[1], wifi_info.ap_ip_addr[0]);
	esp_netif_set_ip4_addr(&ip_info.gw, wifi_info.ap_ip_addr[3], wifi_info.ap_ip_addr[2],
	                       wifi_info.ap_ip_addr[1], wifi_info.ap_ip_addr[0]);
	esp_netif_set_ip4_addr(&ip_info.netmask, 255, 255, 255, 0);

	(void) esp_netif_dhcps_stop(netif);
	ret = esp_netif_set_ip_info(netif, &ip_info);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Could not set AP IP address (%d)", ret);
		return false;
	}
	(void) esp_netif_dhcps_start(netif);

	return true;
}


/**
 * Fill in the SoftAP configuration from the camera's stored identity
 */
static void load_ap_wifi_config(wifi_config_t* cfg)
{
	memset(cfg, 0, sizeof(wifi_config_t));
	cfg->ap.ssid_len = strlen(wifi_info.ap_ssid);
	cfg->ap.max_connection = WIFI_AP_MAX_CONN;
	cfg->ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
	strcpy((char*) cfg->ap.ssid, wifi_info.ap_ssid);
	strcpy((char*) cfg->ap.password, wifi_info.ap_pw);
	if (strlen(wifi_info.ap_pw) == 0) {
		cfg->ap.authmode = WIFI_AUTH_OPEN;
	}
}


/**
 * Raise the recovery access point.  Called from the station retry path when the
 * configured network has been unreachable for a while - the usual reason being
 * that the camera moved to a new location.  The station keeps retrying in the
 * background, so if the configured network comes back (a rebooted router) the
 * camera rejoins it and the recovery AP dissolves; meanwhile anyone can join the
 * camera's own network and re-provision it from the captive portal.
 *
 * Runtime-only: nothing here touches persistent storage.
 */
static void enable_fallback_ap()
{
	int i;
	wifi_config_t wifi_config;

	if (fallback_active) return;

	ESP_LOGI(TAG, "Cannot reach '%s' - raising recovery AP '%s' at %d.%d.%d.%d",
	         wifi_info.sta_ssid, wifi_info.ap_ssid,
	         wifi_info.ap_ip_addr[3], wifi_info.ap_ip_addr[2],
	         wifi_info.ap_ip_addr[1], wifi_info.ap_ip_addr[0]);

	// The AP netif is created once and reused across fallback episodes
	if (fallback_netif == NULL) {
		fallback_netif = esp_netif_create_default_wifi_ap();
		if (fallback_netif == NULL) {
			ESP_LOGE(TAG, "Could not create recovery AP netif");
			return;
		}
	}
	if (!apply_ap_ip_config(fallback_netif)) return;

	load_ap_wifi_config(&wifi_config);

	if (esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK) {
		ESP_LOGE(TAG, "Could not enter APSTA for recovery");
		return;
	}
	if (esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config) != ESP_OK) {
		ESP_LOGE(TAG, "Could not configure recovery AP");
		return;
	}

	// Present the AP address as ours and mark the interface usable so the web
	// server (which waits for connectivity at boot) comes up and can serve the
	// portal.  Clients joining the AP get the same captive-portal flow as a
	// factory-fresh camera.
	for (i = 0; i < 4; i++) {
		wifi_info.cur_ip_addr[i] = wifi_info.ap_ip_addr[i];
	}
	wifi_info.flags |= NET_INFO_FLAG_CONNECTED;

	dns_hijack_start(((uint32_t) wifi_info.ap_ip_addr[3])       |
	                 ((uint32_t) wifi_info.ap_ip_addr[2] << 8)  |
	                 ((uint32_t) wifi_info.ap_ip_addr[1] << 16) |
	                 ((uint32_t) wifi_info.ap_ip_addr[0] << 24));

	fallback_active = true;
}


/**
 * Dissolve the recovery access point after the station successfully joined a
 * network.  Joined clients are dropped - expected, since either the original
 * network returned or they just re-provisioned the camera onto a new one.
 */
static void disable_fallback_ap()
{
	if (!fallback_active) return;

	ESP_LOGI(TAG, "Station connected - dissolving recovery AP");

	dns_hijack_stop();
	(void) esp_wifi_set_mode(WIFI_MODE_STA);
	fallback_active = false;
}


bool wifi_is_fallback_active()
{
	return fallback_active;
}
