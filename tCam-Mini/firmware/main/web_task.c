/*
 * Web Server Task
 *
 * See web_task.h for a description of this module's role.
 *
 * The UI itself is a single gzipped HTML file linked into the firmware image.
 * Keeping it in the application partition rather than a separate filesystem means
 * an OTA update replaces the firmware and its UI atomically - the two can never be
 * left at mismatched versions, which is the usual failure of split-image designs.
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
 *
 */
#include "web_task.h"
#include "web_cmd.h"
#include "client_if.h"
#include "dns_hijack.h"
#include "log_ring.h"
#include "ctrl_task.h"
#include "net_utilities.h"
#include "wifi_utilities.h"
#include "system_config.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "nvs.h"
// IDF 5's esp_http_server.h no longer drags the socket API in behind it, and
// peer_str() below works directly on the session's socket
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>


//
// Web Task constants
//

// Chunk used to drain an OTA upload out of the socket
#define OTA_RX_CHUNK 1024

// Upper bound on networks returned by a scan
#define SCAN_MAX_AP 20

// Camera discovery.  Deliberately short and strictly on demand: mdns_query_ptr()
// blocks the server task for its full timeout, so this must never run on a timer.
// Image frames are unaffected either way - rsp_task sends them straight onto the
// socket rather than through the server task.
#define DISCOVER_TIMEOUT_MS 1500
#define DISCOVER_MAX_RESULTS 8


//
// Web Task variables
//
static const char* TAG = "web_task";

static httpd_handle_t server = NULL;

// Written by the server task during an upload, polled by rsp_task
static volatile bool ota_active = false;

// The UI, gzipped and linked in by EMBED_FILES in CMakeLists.txt
extern const uint8_t index_html_gz_start[] asm("_binary_index_html_gz_start");
extern const uint8_t index_html_gz_end[]   asm("_binary_index_html_gz_end");
extern const uint8_t icon192_start[] asm("_binary_icon_192_png_start");
extern const uint8_t icon192_end[]   asm("_binary_icon_192_png_end");
extern const uint8_t icon512_start[] asm("_binary_icon_512_png_start");
extern const uint8_t icon512_end[]   asm("_binary_icon_512_png_end");


//
// Web Task forward declarations
//
static esp_err_t index_get_handler(httpd_req_t* req);
static esp_err_t status_get_handler(httpd_req_t* req);
static esp_err_t scan_get_handler(httpd_req_t* req);
static esp_err_t ota_post_handler(httpd_req_t* req);
static esp_err_t manifest_get_handler(httpd_req_t* req);
static esp_err_t icon_get_handler(httpd_req_t* req);
static esp_err_t discover_get_handler(httpd_req_t* req);
static esp_err_t log_get_handler(httpd_req_t* req);
static esp_err_t redirect_handler(httpd_req_t* req, httpd_err_code_t err);
static void register_handlers(httpd_handle_t hd);
static void register_uri(httpd_handle_t hd, const httpd_uri_t* uri);
static bool start_webserver();
static void retire_tls_state();
static int count_connections(httpd_handle_t hd);


//
// Web Task API
//
void web_task()
{
	net_info_t* net_infoP;

	ESP_LOGI(TAG, "Start task");

#ifndef WEB_VERBOSE_NET_LOGS
	// Quiet the log lines that normal operation produces in bulk (dropped
	// connections, malformed probe requests).  Note which tags are NOT quieted.
	// httpd_uri warns - only warns - when the handler table is full, and
	// silencing that hid a missing /ws endpoint behind a symptom that looked
	// like a network fault for several releases.  httpd keeps its errors for the
	// same reason: a server that fails to bind should say so.
	esp_log_level_set("httpd", ESP_LOG_ERROR);
	esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
	esp_log_level_set("httpd_uri", ESP_LOG_WARN);
	esp_log_level_set("httpd_parse", ESP_LOG_ERROR);
#endif

	// The listening socket cannot be bound before the interface has an address
	while (!(*net_is_connected)()) {
		vTaskDelay(pdMS_TO_TICKS(500));
	}

	// Internal heap is the scarce resource here - task stacks and socket buffers
	// must come from it.  Log it so a failure to start leaves a diagnosable trail.
	ESP_LOGI(TAG, "Internal heap: %d free, largest block %d",
	         heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
	         heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));

	// Retry startup a few times: right after a WPA3 association the supplicant's
	// handshake allocations can still be in flight, and a moment later the same
	// start succeeds.  Persistent failure is a real fault.
	{
		int attempt = 0;

		while (!start_webserver()) {
			if (++attempt >= 5) {
				ESP_LOGE(TAG, "Could not start web server after %d attempts", attempt);
				ctrl_set_fault_type(CTRL_FAULT_NETWORK);
				vTaskDelete(NULL);
				return;
			}
			ESP_LOGE(TAG, "Web server start failed - retrying");
			vTaskDelay(pdMS_TO_TICKS(2000));
		}
	}

	ESP_LOGI(TAG, "Internal heap after server start: %d free, largest block %d",
	         heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
	         heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));

	// Housekeeping from the removed HTTPS support (6.6-6.8): cameras that ran it
	// carry a CA and certificate in NVS.  Reclaim that space.
	retire_tls_state();

	// Only hijack DNS while we are the access point.  On someone else's network
	// there is a real resolver and answering for every name would be hostile.
	// Note: cur_ip_addr index 3 holds the FIRST octet (see ps_utilities), and the
	// first octet is the lowest byte in network order.
	if (wifi_is_ap_mode()) {
		net_infoP = (*net_get_info)();
		dns_hijack_start(((uint32_t) net_infoP->cur_ip_addr[3])       |
		                 ((uint32_t) net_infoP->cur_ip_addr[2] << 8)  |
		                 ((uint32_t) net_infoP->cur_ip_addr[1] << 16) |
		                 ((uint32_t) net_infoP->cur_ip_addr[0] << 24));
		ESP_LOGI(TAG, "Captive portal active on %d.%d.%d.%d",
		         net_infoP->cur_ip_addr[3], net_infoP->cur_ip_addr[2],
		         net_infoP->cur_ip_addr[1], net_infoP->cur_ip_addr[0]);
	}

	// The server runs on its own task from here; nothing left to do but stay alive
	while (1) {
		vTaskDelay(pdMS_TO_TICKS(10000));
	}
}


bool web_ota_in_progress()
{
	return ota_active;
}


//
// Web Task internal functions
//
static bool start_webserver()
{
	esp_err_t ret;
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();

	config.server_port      = WEB_PORT;
	config.ctrl_port        = 32768;
	config.stack_size        = WEB_TASK_STACK_SIZE;
	config.max_open_sockets  = WEB_MAX_SOCKETS;
	config.max_uri_handlers  = WEB_MAX_URI_HANDLERS;
	config.lru_purge_enable  = true;
	// Wildcard matching lets one handler answer every captive-portal probe URL
	config.uri_match_fn     = httpd_uri_match_wildcard;
	// Long enough that a slow phone does not get dropped mid-upload
	config.recv_wait_timeout = 15;
	config.send_wait_timeout = 15;
	config.close_fn          = web_cmd_close_fn;

	ret = httpd_start(&server, &config);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "httpd_start failed (%d)", ret);
		return false;
	}

	register_handlers(server);

	ESP_LOGI(TAG, "Web server started on port %d", WEB_PORT);
	return true;
}


static void register_handlers(httpd_handle_t hd)
{
	esp_err_t ret;

	static const httpd_uri_t index_uri = {
		.uri = "/", .method = HTTP_GET, .handler = index_get_handler, .user_ctx = NULL
	};
	static const httpd_uri_t status_uri = {
		.uri = "/api/status", .method = HTTP_GET, .handler = status_get_handler, .user_ctx = NULL
	};
	static const httpd_uri_t scan_uri = {
		.uri = "/api/scan", .method = HTTP_GET, .handler = scan_get_handler, .user_ctx = NULL
	};
	static const httpd_uri_t ota_uri = {
		.uri = "/api/ota", .method = HTTP_POST, .handler = ota_post_handler, .user_ctx = NULL
	};

	static const httpd_uri_t manifest_uri = {
		.uri = "/manifest.webmanifest", .method = HTTP_GET, .handler = manifest_get_handler, .user_ctx = NULL
	};
	static const httpd_uri_t icon192_uri = {
		.uri = "/icon-192.png", .method = HTTP_GET, .handler = icon_get_handler, .user_ctx = (void*) 192
	};
	static const httpd_uri_t icon512_uri = {
		.uri = "/icon-512.png", .method = HTTP_GET, .handler = icon_get_handler, .user_ctx = (void*) 512
	};
	static const httpd_uri_t discover_uri = {
		.uri = "/api/discover", .method = HTTP_GET, .handler = discover_get_handler, .user_ctx = NULL
	};
	static const httpd_uri_t log_uri = {
		.uri = "/api/log", .method = HTTP_GET, .handler = log_get_handler, .user_ctx = NULL
	};

	// The WebSocket goes in first.  It carries the image stream, so if the table
	// ever runs short again it must not be the endpoint that loses its slot.
	ret = web_cmd_register(hd);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Could not register /ws (%s) - the image stream cannot run "
		              "without it", esp_err_to_name(ret));
	}

	register_uri(hd, &index_uri);
	register_uri(hd, &status_uri);
	register_uri(hd, &scan_uri);
	register_uri(hd, &ota_uri);
	register_uri(hd, &manifest_uri);
	register_uri(hd, &icon192_uri);
	register_uri(hd, &icon512_uri);
	register_uri(hd, &discover_uri);
	register_uri(hd, &log_uri);

	// Anything else - including every OS connectivity probe - is bounced to the UI
	httpd_register_err_handler(hd, HTTPD_404_NOT_FOUND, redirect_handler);
}


/**
 * Install one handler, reporting failure.  A handler that does not install is
 * invisible at runtime - the request simply falls through to the 404 redirect,
 * which answers it with something that looks like a working server - so the
 * registration has to say so itself.
 */
static void register_uri(httpd_handle_t hd, const httpd_uri_t* uri)
{
	esp_err_t ret = httpd_register_uri_handler(hd, uri);

	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Could not register %s (%s)", uri->uri, esp_err_to_name(ret));
	}
}


/**
 * Erase the certificate material left behind by the removed HTTPS support.  A
 * camera upgraded from 6.6-6.8 holds a CA, its private key and a leaf
 * certificate in NVS - several KB of a small partition doing nothing.  Runs
 * every boot; on a camera that never had them the namespace does not exist and
 * this returns immediately.
 */
static void retire_tls_state()
{
	nvs_handle_t h;

	// The READONLY probe fails if the namespace was never created; a READWRITE
	// open would create it as a side effect on a camera that never had one
	if (nvs_open("tcamtls", NVS_READONLY, &h) != ESP_OK) return;
	nvs_close(h);

	if (nvs_open("tcamtls", NVS_READWRITE, &h) != ESP_OK) return;

	if (nvs_erase_all(h) == ESP_OK) {
		nvs_commit(h);
		ESP_LOGI(TAG, "Removed stored TLS certificates (HTTPS support retired)");
	}
	nvs_close(h);
}


/**
 * Format the remote end of a request's socket as "a.b.c.d:port".  Diagnostic
 * aid: when something on the network misbehaves against the camera, the log
 * should name an address, not leave the operator guessing which device it is.
 */
static void peer_str(int fd, char* out, size_t out_len)
{
	struct sockaddr_storage addr;
	socklen_t alen = sizeof(addr);

	out[0] = 0;
	if (getpeername(fd, (struct sockaddr*) &addr, &alen) == 0) {
		if (addr.ss_family == AF_INET) {
			struct sockaddr_in* a4 = (struct sockaddr_in*) &addr;
			snprintf(out, out_len, "%s:%d", inet_ntoa(a4->sin_addr),
			         (int) ntohs(a4->sin_port));
			return;
		}
#if CONFIG_LWIP_IPV6
		// With IPv6 enabled the server listens on an IPv6 socket, and IPv4
		// clients arrive as v4-mapped addresses (::ffff:a.b.c.d).  This is the
		// common case, not the exception - missing it made every peer "unknown".
		if (addr.ss_family == AF_INET6) {
			struct sockaddr_in6* a6 = (struct sockaddr_in6*) &addr;
			const uint8_t* b = (const uint8_t*) &a6->sin6_addr;
			int i, zeros = 1;

			for (i = 0; i < 10; i++) if (b[i] != 0) zeros = 0;
			if (zeros && (b[10] == 0xFF) && (b[11] == 0xFF)) {
				snprintf(out, out_len, "%d.%d.%d.%d:%d", b[12], b[13], b[14], b[15],
				         (int) ntohs(a6->sin6_port));
				return;
			}
			snprintf(out, out_len, "ipv6 peer");
			return;
		}
#endif
	}
	snprintf(out, out_len, "unknown");
}


static esp_err_t index_get_handler(httpd_req_t* req)
{
	char peer[32];

	// Page loads are rare and identifying - log who is using the camera, so the
	// log can distinguish the operator's devices from anything else on the
	// network poking at the server
	peer_str(httpd_req_to_sockfd(req), peer, sizeof(peer));
	ESP_LOGI(TAG, "UI page fetched by %s", peer);

	httpd_resp_set_type(req, "text/html");
	httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
	// The UI ships with the firmware, so it is safe to cache until the next update
	httpd_resp_set_hdr(req, "Cache-Control", "max-age=600");

	return httpd_resp_send(req, (const char*) index_html_gz_start,
	                       index_html_gz_end - index_html_gz_start);
}


/**
 * Count the sockets currently open on a server instance.  Used as the "how many
 * clients are talking to the camera" figure: a browser holds one for the page and
 * one for the WebSocket, so this is connections rather than people, and the UI
 * labels it accordingly.
 */
static int count_connections(httpd_handle_t hd)
{
	int fds[WEB_MAX_SOCKETS];
	size_t num = WEB_MAX_SOCKETS;

	if (hd == NULL) return 0;
	if (httpd_get_client_list(hd, &num, fds) != ESP_OK) return 0;

	return (int) num;
}


static esp_err_t status_get_handler(httpd_req_t* req)
{
	char buf[512];
	const esp_app_desc_t* app_desc;
	int brd_type;
	int if_type;
	int len;
	int rssi = 0;
	int stations = -1;
	net_info_t* net_infoP;
	static const char* session_names[] = { "none", "tcp", "web" };

	app_desc = esp_app_get_description();
	net_infoP = (*net_get_info)();
	ctrl_get_if_mode(&brd_type, &if_type);

	// The recovery access point behaves as AP mode regardless of what the stored
	// configuration says - report it that way so the UI shows the right data
	bool ap_like = wifi_is_ap_mode() || wifi_is_fallback_active();

	// Signal strength means different things depending on which side we are on.
	// As a station it is our link to the router.  As an access point we have no
	// single link, so report the strongest associated station - in practice the
	// device closest to the camera, which is usually the one being held.
	if (if_type == CTRL_IF_MODE_WIFI) {
		if (ap_like) {
			static wifi_sta_list_t sta_list;

			if (esp_wifi_ap_get_sta_list(&sta_list) == ESP_OK) {
				int i;

				stations = sta_list.num;
				for (i = 0; i < sta_list.num; i++) {
					if ((i == 0) || (sta_list.sta[i].rssi > rssi)) {
						rssi = sta_list.sta[i].rssi;
					}
				}
			}
		} else {
			wifi_ap_record_t ap;

			if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
				rssi = ap.rssi;
			}
		}
	}

	len = snprintf(buf, sizeof(buf),
		"{\"camera\":\"%s\",\"version\":\"%s\",\"model\":%d,"
		"\"interface\":\"%s\",\"mode\":\"%s\",\"ip\":\"%d.%d.%d.%d\","
		"\"sta_ssid\":\"%s\",\"ota\":%s,"
		"\"rssi\":%d,\"stations\":%d,\"connections\":%d,\"session\":\"%s\","
		"\"heap\":%d}",
		net_infoP->ap_ssid,
		app_desc->version,
		(brd_type == CTRL_BRD_ETH_TYPE) ? CAMERA_MODEL_NUM_ETH : CAMERA_MODEL_NUM_WIFI,
		(if_type == CTRL_IF_MODE_ETH) ? "Ethernet" : "WiFi",
		ap_like ? "ap" : "sta",
		net_infoP->cur_ip_addr[3], net_infoP->cur_ip_addr[2],
		net_infoP->cur_ip_addr[1], net_infoP->cur_ip_addr[0],
		net_infoP->sta_ssid,
		ota_active ? "true" : "false",
		rssi,
		stations,
		count_connections(server),
		session_names[client_if_active()],
		heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));

	httpd_resp_set_type(req, "application/json");
	return httpd_resp_send(req, buf, len);
}


static esp_err_t scan_get_handler(httpd_req_t* req)
{
	char entry[128];
	esp_err_t ret;
	uint16_t emitted;
	uint16_t i;
	uint16_t num = SCAN_MAX_AP;
	static wifi_ap_record_t records[SCAN_MAX_AP];
	wifi_scan_config_t scan_cfg = {
		.ssid = NULL, .bssid = NULL, .channel = 0, .show_hidden = false,
		.scan_type = WIFI_SCAN_TYPE_ACTIVE
	};

	// A scan takes a couple of seconds and stalls the radio, which would visibly
	// stutter a live stream, so this is only offered on demand from the settings UI.
	//
	// The bracket matters: the driver refuses to scan while the station is trying
	// to connect, and a camera whose configured network is unreachable - the
	// recovery-AP situation, where the user most needs this list - is trying to
	// connect nearly all the time.  prepare() aborts the attempt and holds
	// reconnects off until complete().
	wifi_scan_prepare();
	vTaskDelay(pdMS_TO_TICKS(150));   // let an aborted connect settle

	ret = esp_wifi_scan_start(&scan_cfg, true);
	if (ret != ESP_OK) {
		// The driver can still be tearing the connect down; one deliberate retry
		vTaskDelay(pdMS_TO_TICKS(400));
		ret = esp_wifi_scan_start(&scan_cfg, true);
	}
	if (ret != ESP_OK) {
		wifi_scan_complete();
		ESP_LOGE(TAG, "scan failed (%d)", ret);
		httpd_resp_set_type(req, "application/json");
		return httpd_resp_sendstr(req, "{\"error\":\"scan failed\",\"networks\":[]}");
	}

	esp_wifi_scan_get_ap_records(&num, records);
	wifi_scan_complete();

	httpd_resp_set_type(req, "application/json");
	httpd_resp_sendstr_chunk(req, "{\"networks\":[");

	emitted = 0;
	for (i = 0; i < num; i++) {
		// Skip unnamed networks - they cannot be selected from a list anyway
		if (records[i].ssid[0] == 0) continue;

		// Separator keys off what has actually been emitted, not the loop index,
		// so a skipped first entry cannot produce a leading comma
		snprintf(entry, sizeof(entry), "%s{\"ssid\":\"%s\",\"rssi\":%d,\"open\":%s}",
		         (emitted == 0) ? "" : ",",
		         (char*) records[i].ssid,
		         records[i].rssi,
		         (records[i].authmode == WIFI_AUTH_OPEN) ? "true" : "false");
		httpd_resp_sendstr_chunk(req, entry);
		emitted++;
	}

	httpd_resp_sendstr_chunk(req, "]}");
	return httpd_resp_sendstr_chunk(req, NULL);
}


/**
 * Firmware update by plain HTTP POST of a .bin file.
 *
 * This replaces the previous scheme, in which the camera asked the host for the
 * next chunk over the json protocol and gave up after a fixed number of retries.
 * That inverted the normal direction of control, needed a bespoke implementation in
 * every client, and failed in ways the user could not act on.  Here the browser
 * simply uploads the image; flow control is TCP's problem, and a failed upload
 * leaves the running partition untouched.
 */
static esp_err_t ota_post_handler(httpd_req_t* req)
{
	char buf[OTA_RX_CHUNK];
	esp_err_t ret;
	esp_ota_handle_t ota_handle = 0;
	const esp_partition_t* update_partition;
	int received;
	int remaining = req->content_len;

	if (remaining <= 0) {
		httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty upload");
		return ESP_FAIL;
	}

	update_partition = esp_ota_get_next_update_partition(NULL);
	if (update_partition == NULL) {
		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
		return ESP_FAIL;
	}

	if (remaining > (int) update_partition->size) {
		httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Image larger than partition");
		return ESP_FAIL;
	}

	ESP_LOGI(TAG, "OTA start: %d bytes -> partition at 0x%x", remaining,
	         (unsigned int) update_partition->address);

	ret = esp_ota_begin(update_partition, remaining, &ota_handle);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "esp_ota_begin failed (%d)", ret);
		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not begin update");
		return ESP_FAIL;
	}

	ota_active = true;

	while (remaining > 0) {
		received = httpd_req_recv(req, buf, (remaining < OTA_RX_CHUNK) ? remaining : OTA_RX_CHUNK);
		if (received <= 0) {
			if (received == HTTPD_SOCK_ERR_TIMEOUT) continue;
			ESP_LOGE(TAG, "OTA receive failed");
			esp_ota_abort(ota_handle);
			ota_active = false;
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Upload interrupted");
			return ESP_FAIL;
		}

		ret = esp_ota_write(ota_handle, buf, received);
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "esp_ota_write failed (%d)", ret);
			esp_ota_abort(ota_handle);
			ota_active = false;
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Flash write failed");
			return ESP_FAIL;
		}

		remaining -= received;
	}

	// esp_ota_end validates the image; a truncated or corrupt upload is rejected
	// here and the currently running firmware is left as the boot target
	ret = esp_ota_end(ota_handle);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "esp_ota_end failed (%d)", ret);
		ota_active = false;
		httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Image failed validation");
		return ESP_FAIL;
	}

	ret = esp_ota_set_boot_partition(update_partition);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%d)", ret);
		ota_active = false;
		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not set boot partition");
		return ESP_FAIL;
	}

	ESP_LOGI(TAG, "OTA complete - restarting");
	httpd_resp_set_type(req, "application/json");
	httpd_resp_sendstr(req, "{\"status\":\"ok\",\"restarting\":true}");

	// Give the response time to reach the browser before the reset
	vTaskDelay(pdMS_TO_TICKS(1000));
	esp_restart();

	return ESP_OK;
}


/**
 * Bounce unknown URLs to the UI.  Operating systems fetch a known probe URL right
 * after joining a network; a 302 to our root is what makes the phone or laptop
 * offer to open the page by itself.
 */
static esp_err_t redirect_handler(httpd_req_t* req, httpd_err_code_t err)
{
	char body[384];
	char location[40];
	net_info_t* net_infoP = (*net_get_info)();

	(void) err;   // every 404 gets the same treatment

	// Name what was asked for.  An unregistered endpoint is answered here rather
	// than refused, so without this line a missing route is indistinguishable
	// from a working one - which is exactly how a missing /ws stayed hidden.
	ESP_LOGI(TAG, "No handler for '%s' - redirecting", req->uri);

	// cur_ip_addr index 3 holds the first octet (see ps_utilities)
	snprintf(location, sizeof(location), "http://%d.%d.%d.%d/",
	         net_infoP->cur_ip_addr[3], net_infoP->cur_ip_addr[2],
	         net_infoP->cur_ip_addr[1], net_infoP->cur_ip_addr[0]);

	httpd_resp_set_status(req, "302 Found");
	httpd_resp_set_hdr(req, "Location", location);

	// A body as well as the header.  Windows' captive-portal window and several
	// phone portal browsers render the response instead of following the
	// redirect, and an empty document leaves them sitting on a blank page (or
	// their own home page), which reads as the portal having failed.
	snprintf(body, sizeof(body),
	         "<!doctype html><meta http-equiv=refresh content=\"0;url=%s\">"
	         "<body style=\"background:#0b0e13;color:#eef2f7;font-family:sans-serif\">"
	         "<a style=\"color:#ff7a1a\" href=\"%s\">Open the camera</a></body>",
	         location, location);
	httpd_resp_set_type(req, "text/html");
	httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);

	return ESP_OK;
}


/**
 * Web app manifest.  Generated rather than embedded so the installed app carries
 * this camera's own name - with several cameras on a bench, identical home screen
 * icons would be useless.
 *
 * Note on installability: a service worker (and therefore Chrome's full "Install
 * app" flow) requires a trusted secure context, which a plain-http camera on a
 * LAN is not.  Instead, Android offers "Add to Home screen" and iOS honours the
 * apple-mobile-web-app meta tags in the page.  Both give a home screen icon that
 * launches the UI chrome-free, which is the part that matters.  Offline caching
 * is no loss here: the camera serves this app, so if the camera is unreachable
 * there is nothing for a cached app to talk to.
 */
static esp_err_t manifest_get_handler(httpd_req_t* req)
{
	char buf[768];
	int len;
	net_info_t* net_infoP = (*net_get_info)();

	// id gives the installed app a stable identity independent of the entry URL,
	// and launch_handler makes opening the camera focus the already-running app
	// window instead of stacking a second copy - the closest the web platform
	// gets to "launch the installed app".
	len = snprintf(buf, sizeof(buf),
		"{\"name\":\"%s Thermal Camera\",\"short_name\":\"%s\","
		"\"id\":\"/\","
		"\"start_url\":\"/\",\"scope\":\"/\",\"display\":\"standalone\","
		"\"launch_handler\":{\"client_mode\":\"focus-existing\"},"
		"\"orientation\":\"any\",\"background_color\":\"#07090d\","
		"\"theme_color\":\"#07090d\","
		"\"description\":\"Live thermal viewer and configuration for %s\","
		"\"icons\":["
		"{\"src\":\"/icon-192.png\",\"sizes\":\"192x192\",\"type\":\"image/png\","
		"\"purpose\":\"any maskable\"},"
		"{\"src\":\"/icon-512.png\",\"sizes\":\"512x512\",\"type\":\"image/png\","
		"\"purpose\":\"any maskable\"}]}",
		net_infoP->ap_ssid, net_infoP->ap_ssid, net_infoP->ap_ssid);

	httpd_resp_set_type(req, "application/manifest+json");
	httpd_resp_set_hdr(req, "Cache-Control", "max-age=600");
	return httpd_resp_send(req, buf, len);
}


static esp_err_t icon_get_handler(httpd_req_t* req)
{
	const uint8_t* start;
	const uint8_t* end;

	if ((int) req->user_ctx == 512) {
		start = icon512_start; end = icon512_end;
	} else {
		start = icon192_start; end = icon192_end;
	}

	httpd_resp_set_type(req, "image/png");
	// Icons ship with the firmware, so they can be cached until the next update
	httpd_resp_set_hdr(req, "Cache-Control", "max-age=86400");
	return httpd_resp_send(req, (const char*) start, end - start);
}


/**
 * Find other tCam cameras on this network by asking mDNS for the service every
 * camera already advertises.  The camera does the discovery rather than the
 * browser because browsers cannot speak mDNS, and the alternative - having the
 * page probe every address on the subnet - is both slow and hostile to the LAN.
 *
 * Strictly on demand.  mdns_query_ptr() blocks this task for its timeout, which
 * would stutter other HTTP requests if it ran on a timer; image frames are not
 * affected, because rsp_task writes them straight to the socket.
 */
/**
 * Serve the buffered system log as plain text - the serial console without the
 * cable.  The linearized copy is allocated per request (PSRAM).
 */
static esp_err_t log_get_handler(httpd_req_t* req)
{
	char* buf;
	size_t len;
	esp_err_t ret;

	buf = heap_caps_malloc(LOG_RING_SIZE + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	if (buf == NULL) {
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
	}

	len = log_ring_copy(buf, LOG_RING_SIZE + 1);

	httpd_resp_set_type(req, "text/plain; charset=utf-8");
	httpd_resp_set_hdr(req, "Cache-Control", "no-store");
	ret = httpd_resp_send(req, buf, len);

	heap_caps_free(buf);
	return ret;
}


static esp_err_t discover_get_handler(httpd_req_t* req)
{
	char entry[224];
	char self_ip[16];
	esp_err_t ret;
	mdns_result_t* results = NULL;
	mdns_result_t* r;
	net_info_t* net_infoP = (*net_get_info)();
	const esp_app_desc_t* app_desc = esp_app_get_description();

	// This camera goes in the list unconditionally.  mDNS deliberately does not
	// answer its own queries, so on a network with one camera - and always when
	// the client is connected straight to the camera's own AP - a pure query
	// returns nothing and the picker looked broken.  The UI recognises the
	// address it is already talking to and marks the entry "this camera".
	// cur_ip_addr index 3 holds the first octet (see ps_utilities).
	snprintf(self_ip, sizeof(self_ip), "%d.%d.%d.%d",
	         net_infoP->cur_ip_addr[3], net_infoP->cur_ip_addr[2],
	         net_infoP->cur_ip_addr[1], net_infoP->cur_ip_addr[0]);

	httpd_resp_set_type(req, "application/json");
	httpd_resp_sendstr_chunk(req, "{\"cameras\":[");

	snprintf(entry, sizeof(entry),
	         "{\"name\":\"%s\",\"host\":\"%s\",\"ip\":\"%s\",\"version\":\"%s\"}",
	         net_infoP->ap_ssid, net_infoP->ap_ssid, self_ip, app_desc->version);
	httpd_resp_sendstr_chunk(req, entry);

	// Then whatever mDNS can see.  A query failure is logged but no longer
	// reported as a failed discovery - the list above is already useful.
	ret = mdns_query_ptr("_tcam-socket", "_tcp", DISCOVER_TIMEOUT_MS,
	                     DISCOVER_MAX_RESULTS, &results);
	if (ret != ESP_OK) {
		ESP_LOGW(TAG, "mDNS query failed (%d)", ret);
		results = NULL;
	}

	for (r = results; r != NULL; r = r->next) {
		char rip[16];
		const char* version = "";
		mdns_ip_addr_t* a;
		size_t t;
		uint32_t ip = 0;

		// Only IPv4 is useful for building a URL the browser can open
		for (a = r->addr; a != NULL; a = a->next) {
			if (a->addr.type == ESP_IPADDR_TYPE_V4) {
				ip = a->addr.u_addr.ip4.addr;
				break;
			}
		}
		if (ip == 0) continue;

		snprintf(rip, sizeof(rip), "%d.%d.%d.%d",
		         (int) (ip & 0xFF), (int) ((ip >> 8) & 0xFF),
		         (int) ((ip >> 16) & 0xFF), (int) ((ip >> 24) & 0xFF));

		// Already emitted above as the unconditional self entry
		if (strcmp(rip, self_ip) == 0) continue;

		for (t = 0; t < r->txt_count; t++) {
			if ((r->txt[t].key != NULL) && (strcmp(r->txt[t].key, "version") == 0) &&
			    (r->txt[t].value != NULL)) {
				version = r->txt[t].value;
			}
		}

		snprintf(entry, sizeof(entry),
		         ",{\"name\":\"%s\",\"host\":\"%s\",\"ip\":\"%s\",\"version\":\"%s\"}",
		         (r->instance_name != NULL) ? r->instance_name : "tCam",
		         (r->hostname != NULL) ? r->hostname : "",
		         rip, version);
		httpd_resp_sendstr_chunk(req, entry);
	}

	mdns_query_results_free(results);

	httpd_resp_sendstr_chunk(req, "]}");
	return httpd_resp_sendstr_chunk(req, NULL);
}
