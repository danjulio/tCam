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

// One announcement per connection when the first image frame actually goes out.
// The stream failing is otherwise invisible from the camera side: every stage
// before the socket looks identical whether the browser is rendering frames or
// showing a black rectangle.
static bool first_frame_logged = false;
static bool first_cmd_logged = false;


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

	if (!first_frame_logged) {
		first_frame_logged = true;
		ESP_LOGI(TAG, "First image frame sent to ws client %d (%d bytes)", fd, len);
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
	first_frame_logged = false;
	first_cmd_logged = false;
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


/**
 * Log which peer just took the WebSocket session
 */
static void log_ws_peer(int fd)
{
	struct sockaddr_storage addr;
	socklen_t alen = sizeof(addr);

	if (getpeername(fd, (struct sockaddr*) &addr, &alen) == 0) {
		if (addr.ss_family == AF_INET) {
			struct sockaddr_in* a4 = (struct sockaddr_in*) &addr;
			ESP_LOGI(TAG, "ws client %d connected from %s", fd,
			         inet_ntoa(a4->sin_addr));
			return;
		}
#if CONFIG_LWIP_IPV6
		// IPv4 clients arrive v4-mapped on the IPv6 listener (see peer_str
		// in web_task)
		if (addr.ss_family == AF_INET6) {
			const uint8_t* b = (const uint8_t*)
				&((struct sockaddr_in6*) &addr)->sin6_addr;
			int i, zeros = 1;

			for (i = 0; i < 10; i++) if (b[i] != 0) zeros = 0;
			if (zeros && (b[10] == 0xFF) && (b[11] == 0xFF)) {
				ESP_LOGI(TAG, "ws client %d connected from %d.%d.%d.%d",
				         fd, b[12], b[13], b[14], b[15]);
				return;
			}
		}
#endif
	}
	ESP_LOGI(TAG, "ws client %d connected", fd);
}


/**
 * Claim the command session for the socket a frame just arrived on.  Returns
 * false if a different client (the json TCP port) holds it.
 */
static bool ws_take_session(httpd_handle_t hd, int fd)
{
	if (!client_if_claim(CLIENT_IF_WS)) {
		ESP_LOGW(TAG, "Refusing ws client %d - another client holds the session", fd);
		return false;
	}

	init_command_processor();
	web_cmd_set_client(hd, fd);
	log_ws_peer(fd);
	return true;
}


static esp_err_t ws_handler(httpd_req_t* req)
{
	char delim;
	esp_err_t ret;
	httpd_ws_frame_t frame;
	int fd;
	static uint8_t rx_buf[WEB_CMD_MAX_RX_LEN + 1];

	fd = httpd_req_to_sockfd(req);

	// IDF 4.4 invoked this handler for the handshake GET, and the session used
	// to be claimed here.  Kept for compatibility, but note IDF 5 does NOT take
	// this path - see below.
	if (req->method == HTTP_GET) {
		if (!ws_take_session(req->handle, fd)) {
			// 503 tells the UI to show "camera in use" rather than retrying forever
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
			                    "Camera is in use by another client");
			return ESP_FAIL;
		}
		return ESP_OK;
	}

	// In IDF 5 the server completes the WebSocket handshake internally and
	// deliberately never invokes the URI handler for the upgrade GET
	// (httpd_uri.c: "If the request is websocket handshake, then do not call
	// the uri->handler").  The first data frame is therefore the first time
	// this code learns a client exists - the session must be claimed here.
	// Under the old assumption the ownership check below simply rejected every
	// frame from a client no code had registered, so the server closed each
	// socket after its first command: the browser showed "connected" (the
	// handshake IS completed), then lost the link two seconds later, forever.
	if (fd != ws_fd) {
		if (!ws_take_session(req->handle, fd)) {
			return ESP_FAIL;
		}
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

	// One line for the first inbound command, so the log distinguishes "browser
	// connected but sent nothing" from "commands flowed but streaming never began"
	if (!first_cmd_logged) {
		first_cmd_logged = true;
		ESP_LOGI(TAG, "First command received from ws client %d (%d bytes)",
		         fd, (int) frame.len);
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
