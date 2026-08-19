/*
 * Client Interface Arbitration
 *
 * See client_if.h for a description of this module's role.
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
#include "client_if.h"
#include "net_cmd_task.h"
#include "web_cmd.h"
#include "rsp_task.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include <errno.h>


//
// Client Interface variables
//
static const char* TAG = "client_if";

static SemaphoreHandle_t client_mutex = NULL;
static int active_kind = CLIENT_IF_NONE;


//
// Client Interface API
//
void client_if_init()
{
	if (client_mutex == NULL) {
		client_mutex = xSemaphoreCreateMutex();
	}
	active_kind = CLIENT_IF_NONE;
}


bool client_if_claim(int kind)
{
	bool claimed;

	xSemaphoreTake(client_mutex, portMAX_DELAY);

	if ((active_kind == CLIENT_IF_NONE) || (active_kind == kind)) {
		active_kind = kind;
		claimed = true;
	} else {
		claimed = false;
	}

	xSemaphoreGive(client_mutex);

	if (claimed) {
		ESP_LOGI(TAG, "client %d active", kind);
	} else {
		ESP_LOGI(TAG, "client %d refused (%d holds session)", kind, active_kind);
	}

	return claimed;
}


void client_if_release(int kind)
{
	xSemaphoreTake(client_mutex, portMAX_DELAY);

	if (active_kind == kind) {
		active_kind = CLIENT_IF_NONE;
		ESP_LOGI(TAG, "client %d released", kind);
	}

	xSemaphoreGive(client_mutex);
}


bool client_if_connected()
{
	return (client_if_active() != CLIENT_IF_NONE);
}


int client_if_active()
{
	int kind;

	xSemaphoreTake(client_mutex, portMAX_DELAY);
	kind = active_kind;
	xSemaphoreGive(client_mutex);

	// A transport can drop underneath us (socket reset, browser tab closed) without
	// getting the chance to release, so confirm the holder still believes it is up
	if ((kind == CLIENT_IF_TCP) && !net_cmd_connected()) {
		client_if_release(CLIENT_IF_TCP);
		kind = CLIENT_IF_NONE;
	} else if ((kind == CLIENT_IF_WS) && !web_cmd_connected()) {
		client_if_release(CLIENT_IF_WS);
		kind = CLIENT_IF_NONE;
	}

	return kind;
}


int client_if_send(char* buf, int len)
{
	int byte_offset;
	int err;
	int n;
	int sock;

	switch (client_if_active()) {
		case CLIENT_IF_TCP:
			sock = net_cmd_get_socket();
			if (sock < 0) return -1;

			byte_offset = 0;
			while (byte_offset < len) {
				n = len - byte_offset;
				if (n > RSP_MAX_TX_PKT_LEN) n = RSP_MAX_TX_PKT_LEN;
				err = send(sock, buf + byte_offset, n, 0);
				if (err < 0) {
					ESP_LOGE(TAG, "Error in socket send: errno %d", errno);
					return -1;
				}
				byte_offset += err;
			}
			return byte_offset;

		case CLIENT_IF_WS:
			return web_cmd_send(buf, len);

		default:
			return -1;
	}
}
