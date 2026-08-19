/*
 * BLE Finder Beacon
 *
 * See ble_beacon.h for a description of this module's role.
 *
 * NimBLE rather than Bluedroid: this camera's scarce resource is internal
 * RAM, and the beacon needs only the smallest possible peripheral role -
 * advertise, accept one connection, answer one read.  The classic-BT half of
 * the controller is released outright.
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
#include "ble_beacon.h"
#include "esp_bt.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "net_utilities.h"
#include "wifi_utilities.h"
#include <string.h>


//
// BLE Beacon constants
//

// The finder service and its single characteristic.  These UUIDs are the
// contract with docs/index.html (the hosted finder page) - change one side
// and the other must follow.  NimBLE lists 128-bit UUID bytes least
// significant first:  7ca2c9c0-9d3a-4b2e-8e5d-52f1b74a0c1d
static const ble_uuid128_t finder_svc_uuid = BLE_UUID128_INIT(
	0x1d, 0x0c, 0x4a, 0xb7, 0xf1, 0x52, 0x5d, 0x8e,
	0x2e, 0x4b, 0x3a, 0x9d, 0xc0, 0xc9, 0xa2, 0x7c);

//                                7ca2c9c1-9d3a-4b2e-8e5d-52f1b74a0c1d
static const ble_uuid128_t finder_chr_uuid = BLE_UUID128_INIT(
	0x1d, 0x0c, 0x4a, 0xb7, 0xf1, 0x52, 0x5d, 0x8e,
	0x2e, 0x4b, 0x3a, 0x9d, 0xc1, 0xc9, 0xa2, 0x7c);


//
// BLE Beacon variables
//
static const char* TAG = "ble_beacon";

static uint8_t own_addr_type;


//
// BLE Beacon forward declarations
//
static void ble_advertise();
static int ble_gap_event(struct ble_gap_event* event, void* arg);
static int finder_access(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt* ctxt, void* arg);
static void ble_on_sync();
static void ble_on_reset(int reason);
static void ble_host_task(void* param);


//
// GATT service table: one service, one read-only characteristic
//
static const struct ble_gatt_svc_def finder_svcs[] = {
	{
		.type = BLE_GATT_SVC_TYPE_PRIMARY,
		.uuid = &finder_svc_uuid.u,
		.characteristics = (struct ble_gatt_chr_def[]) {
			{
				.uuid = &finder_chr_uuid.u,
				.access_cb = finder_access,
				.flags = BLE_GATT_CHR_F_READ,
			},
			{ 0 }
		},
	},
	{ 0 }
};


//
// BLE Beacon API
//
bool ble_beacon_init()
{
	esp_err_t eret;
	int rc;
	net_info_t* net_infoP = (*net_get_info)();
	size_t heap_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

	// The classic-BT half of the controller is never used; releasing its
	// memory before the controller starts returns it to the heap for good
	eret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
	if ((eret != ESP_OK) && (eret != ESP_ERR_INVALID_STATE)) {
		ESP_LOGW(TAG, "Classic BT memory release returned %d", eret);
	}

	eret = nimble_port_init();
	if (eret != ESP_OK) {
		ESP_LOGE(TAG, "nimble_port_init failed (%d) - finder beacon disabled", eret);
		return false;
	}

	ble_hs_cfg.sync_cb = ble_on_sync;
	ble_hs_cfg.reset_cb = ble_on_reset;

	ble_svc_gap_init();
	ble_svc_gatt_init();

	rc = ble_gatts_count_cfg(finder_svcs);
	if (rc == 0) {
		rc = ble_gatts_add_svcs(finder_svcs);
	}
	if (rc != 0) {
		ESP_LOGE(TAG, "GATT service registration failed (%d) - finder beacon disabled", rc);
		return false;
	}

	// The BLE device name is the camera name, so the finder page's device
	// picker shows something the user recognizes
	ble_svc_gap_device_name_set(net_infoP->ap_ssid);

	nimble_port_freertos_init(ble_host_task);

	ESP_LOGI(TAG, "Finder beacon started as '%s' (internal heap %u -> %u)",
	         net_infoP->ap_ssid, (unsigned) heap_before,
	         (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
	return true;
}


//
// BLE Beacon internal functions
//

/**
 * Answer a read of the finder characteristic with the camera's current
 * identity and address.  Built fresh on every read - the whole point is that
 * this is the address the camera holds right now.
 */
static int finder_access(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt* ctxt, void* arg)
{
	char buf[128];
	int len;
	net_info_t* net_infoP;

	if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
		return BLE_ATT_ERR_UNLIKELY;
	}

	net_infoP = (*net_get_info)();

	// cur_ip_addr index 3 holds the first octet (see ps_utilities)
	len = snprintf(buf, sizeof(buf),
		"{\"name\":\"%s\",\"ip\":\"%d.%d.%d.%d\",\"mode\":\"%s\"}",
		net_infoP->ap_ssid,
		net_infoP->cur_ip_addr[3], net_infoP->cur_ip_addr[2],
		net_infoP->cur_ip_addr[1], net_infoP->cur_ip_addr[0],
		(wifi_is_ap_mode() || wifi_is_fallback_active()) ? "ap" : "sta");

	return (os_mbuf_append(ctxt->om, buf, len) == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}


/**
 * Start (or restart) advertising: connectable, general-discoverable, with the
 * finder service UUID in the advertisement so the finder page can filter on
 * it, and the full camera name in the scan response.
 */
static void ble_advertise()
{
	struct ble_gap_adv_params adv_params;
	struct ble_hs_adv_fields fields;
	struct ble_hs_adv_fields rsp_fields;
	const char* name;
	int rc;

	memset(&fields, 0, sizeof(fields));
	fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
	fields.uuids128 = (ble_uuid128_t*) &finder_svc_uuid;
	fields.num_uuids128 = 1;
	fields.uuids128_is_complete = 1;

	rc = ble_gap_adv_set_fields(&fields);
	if (rc != 0) {
		ESP_LOGE(TAG, "adv_set_fields failed (%d)", rc);
		return;
	}

	// The 128-bit UUID fills the advertisement, so the name rides in the
	// scan response
	memset(&rsp_fields, 0, sizeof(rsp_fields));
	name = ble_svc_gap_device_name();
	rsp_fields.name = (uint8_t*) name;
	rsp_fields.name_len = strlen(name);
	rsp_fields.name_is_complete = 1;

	rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
	if (rc != 0) {
		ESP_LOGE(TAG, "adv_rsp_set_fields failed (%d)", rc);
	}

	memset(&adv_params, 0, sizeof(adv_params));
	adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
	adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
	// A relaxed interval: discovery is occasional, the WiFi image stream is
	// constant, and the radio is shared.  ~300 ms (units of 0.625 ms).
	adv_params.itvl_min = 480;
	adv_params.itvl_max = 512;

	rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
	                       &adv_params, ble_gap_event, NULL);
	if (rc != 0) {
		ESP_LOGE(TAG, "adv_start failed (%d)", rc);
	}
}


static int ble_gap_event(struct ble_gap_event* event, void* arg)
{
	switch (event->type) {
		case BLE_GAP_EVENT_CONNECT:
			// A failed connection attempt stops advertising; resume.  A
			// successful one also stops it, and ends with DISCONNECT below.
			if (event->connect.status != 0) {
				ble_advertise();
			}
			break;

		case BLE_GAP_EVENT_DISCONNECT:
			ble_advertise();
			break;

		case BLE_GAP_EVENT_ADV_COMPLETE:
			ble_advertise();
			break;

		default:
			break;
	}

	return 0;
}


static void ble_on_sync()
{
	int rc;

	rc = ble_hs_id_infer_auto(0, &own_addr_type);
	if (rc != 0) {
		ESP_LOGE(TAG, "No BLE address available (%d)", rc);
		return;
	}

	ble_advertise();
}


static void ble_on_reset(int reason)
{
	ESP_LOGW(TAG, "BLE host reset (%d)", reason);
}


static void ble_host_task(void* param)
{
	nimble_port_run();
	nimble_port_freertos_deinit();
}
