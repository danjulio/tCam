/*
 * Web Command Bridge
 *
 * See web_cmd.h for a description of this module's role.
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
#include "web_cmd.h"
#include "client_if.h"
#include "cmd_utilities.h"
#include "system_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include <string.h>


//
// Web Command constants
//

// Largest inbound WebSocket message accepted.  The browser never sends firmware
// chunks (OTA is a plain HTTP POST), so this only has to hold a command object.
#define WEB_CMD_MAX_RX_LEN 2048


//
// Web Command variables
//
static const char* TAG = "web_cmd";

// Serialises access to the client identity and to the outbound socket.  rsp_task
// sends image and command responses while the server task services control frames.
static SemaphoreHandle_t web_mutex = NULL;

// Identity of the browser holding the session; ws_fd < 0 means no client
static httpd_handle_t ws_hd = NULL;
static int ws_fd = -1;


//
// Web Command forward declarations
//
static esp_err_t ws_handler(httpd_req_t* req);
static void web_cmd_set_client(httpd_handle_t hd, int fd);
static void web_cmd_clear_client();


//
// Web Command API
//
void web_cmd_init()
{
	if (web_mutex == NULL) {
		web_mutex = xSemaphoreCreateMutex();
	}
	ws_hd = NULL;
	ws_fd = -1;
}


esp_err_t web_cmd_register(httpd_handle_t server)
{
	static const httpd_uri_t ws_uri = {
		.uri          = "/ws",
		.method       = HTTP_GET,
		.handler      = ws_handler,
		.user_ctx     = NULL,
		.is_websocket = true
	};

	return httpd_register_uri_handler(server, &ws_uri);
}


bool web_cmd_connected()
{
	bool up;

	// rsp_task polls the client state from the moment it starts, which can be
	// before the web server task has run, so this must be safe pre-init
	if (web_mutex == NULL) return false;

	xSemaphoreTake(web_mutex, portMAX_DELAY);
	up = (ws_fd >= 0);
	xSemaphoreGive(web_mutex);

	return up;
}


int web_cmd_send(char* buf, int len)
{
	esp_err_t ret;
	httpd_ws_frame_t frame;
	httpd_handle_t hd;
	int fd;

	// The response arrives wrapped in the sentinel bytes the raw TCP transport
	// needs.  A WebSocket message is already delimited, so hand the browser clean
	// json instead of making every client strip control characters.
	if ((len > 0) && (buf[0] == CMD_JSON_STRING_START)) {
		buf++;
		len--;
	}
	if ((len > 0) && (buf[len-1] == CMD_JSON_STRING_STOP)) {
		len--;
	}
	if (len <= 0) return 0;
	if (web_mutex == NULL) return -1;

	xSemaphoreTake(web_mutex, portMAX_DELAY);
	hd = ws_hd;
	fd = ws_fd;

	if (fd < 0) {
		xSemaphoreGive(web_mutex);
		return -1;
	}

	memset(&frame, 0, sizeof(frame));
	frame.type    = HTTPD_WS_TYPE_TEXT;
	frame.payload = (uint8_t*) buf;
	frame.len     = len;
	frame.final   = true;

	ret = httpd_ws_send_frame_async(hd, fd, &frame);

	// Mark the socket as freshly used.  httpd's LRU purge only counts a socket
	// as active when it RECEIVES a request, and a streaming WebSocket is almost
	// pure outbound - so without this, every frame-carrying socket looked idle
	// and was the first thing evicted whenever the browser opened a new HTTP
	// connection (status polls, icons), killing the stream while the settings
	// stayed perfectly live.
	if (ret == ESP_OK) {
		httpd_sess_update_lru_counter(hd, fd);
	}

	xSemaphoreGive(web_mutex);

	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "ws send failed (%d)", ret);
		web_cmd_clear_client();
		return -1;
	}

	return len;
}


int web_cmd_send_binary(char* buf, int len)
{
	esp_err_t ret;
	httpd_ws_frame_t frame;
	httpd_handle_t hd;
	int fd;

	if (len <= 0) return 0;
	if (web_mutex == NULL) return -1;

	xSemaphoreTake(web_mutex, portMAX_DELAY);
	hd = ws_hd;
	fd = ws_fd;

	if (fd < 0) {
		xSemaphoreGive(web_mutex);
		return -1;
	}

	memset(&frame, 0, sizeof(frame));
	frame.type    = HTTPD_WS_TYPE_BINARY;
	frame.payload = (uint8_t*) buf;
	frame.len     = len;
	frame.final   = true;

	ret = httpd_ws_send_frame_async(hd, fd, &frame);

	// Keep the streaming socket off the LRU chopping block (see web_cmd_send)
	if (ret == ESP_OK) {
		httpd_sess_update_lru_counter(hd, fd);
	}

	xSemaphoreGive(web_mutex);

	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "ws binary send failed (%d)", ret);
		web_cmd_clear_client();
		return -1;
	}

	return len;
}


void web_cmd_close_fn(httpd_handle_t hd, int sockfd)
{
	bool was_client;

	(void) hd;

	xSemaphoreTake(web_mutex, portMAX_DELAY);
	was_client = ((sockfd == ws_fd) && (ws_fd >= 0));
	xSemaphoreGive(web_mutex);

	if (was_client) {
		ESP_LOGI(TAG, "ws client %d closed", sockfd);
		web_cmd_clear_client();
	}

	// Perform the default teardown the server would otherwise have done
	close(sockfd);
}


//
// Web Command internal functions
//
static void web_cmd_set_client(httpd_handle_t hd, int fd)
{
	xSemaphoreTake(web_mutex, portMAX_DELAY);
	ws_hd = hd;
	ws_fd = fd;
	xSemaphoreGive(web_mutex);
}


static void web_cmd_clear_client()
{
	xSemaphoreTake(web_mutex, portMAX_DELAY);
	ws_hd = NULL;
	ws_fd = -1;
	xSemaphoreGive(web_mutex);

	client_if_release(CLIENT_IF_WS);
}


static esp_err_t ws_handler(httpd_req_t* req)
{
	char delim;
	esp_err_t ret;
	httpd_ws_frame_t frame;
	int fd;
	static uint8_t rx_buf[WEB_CMD_MAX_RX_LEN + 1];

	fd = httpd_req_to_sockfd(req);

	// The opening GET completes the WebSocket handshake; no frame is present yet
	if (req->method == HTTP_GET) {
		if (!client_if_claim(CLIENT_IF_WS)) {
			ESP_LOGW(TAG, "Refusing ws client - another client holds the session");
			// 503 tells the UI to show "camera in use" rather than retrying forever
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
			                    "Camera is in use by another client");
			return ESP_FAIL;
		}

		init_command_processor();
		web_cmd_set_client(req->handle, fd);
		ESP_LOGI(TAG, "ws client %d connected", fd);
		return ESP_OK;
	}

	// Only the client that owns the session may issue commands
	if (fd != ws_fd) {
		return ESP_FAIL;
	}

	// Read the frame header to learn the payload length
	memset(&frame, 0, sizeof(frame));
	frame.type = HTTPD_WS_TYPE_TEXT;
	ret = httpd_ws_recv_frame(req, &frame, 0);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "ws recv header failed (%d)", ret);
		return ret;
	}

	if (frame.len > WEB_CMD_MAX_RX_LEN) {
		ESP_LOGE(TAG, "ws frame too long (%d)", (int) frame.len);
		return ESP_FAIL;
	}

	if (frame.len > 0) {
		frame.payload = rx_buf;
		ret = httpd_ws_recv_frame(req, &frame, frame.len);
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "ws recv payload failed (%d)", ret);
			return ret;
		}
	}

	if (frame.type == HTTPD_WS_TYPE_CLOSE) {
		web_cmd_clear_client();
		return ESP_OK;
	}

	if ((frame.type != HTTPD_WS_TYPE_TEXT) || (frame.len == 0)) {
		// PING/PONG are handled internally by the server
		return ESP_OK;
	}

	// Re-frame into the sentinel-delimited form the shared parser expects and let
	// the existing command processor do all the work
	delim = CMD_JSON_STRING_START;
	push_rx_data(&delim, 1);
	push_rx_data((char*) rx_buf, frame.len);
	delim = CMD_JSON_STRING_STOP;
	push_rx_data(&delim, 1);

	while (process_rx_data()) {}

	return ESP_OK;
}
