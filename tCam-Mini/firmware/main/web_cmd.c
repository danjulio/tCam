/*
 * Web Command Bridge
 *
 * See web_cmd.h for a description of this module's role.
 *
 * Every connected browser receives the image stream and may issue commands.
 * The single-owner model this replaced turned two open tabs into a fight: each
 * newcomer either stole the session (killing the other viewer's stream) or was
 * refused outright.  A camera on the wall should simply show its picture to
 * whoever asks.  The legacy json TCP client remains exclusive with browser
 * clients, as the two protocols share one command processor state machine.
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
#include "web_task.h"
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

// Simultaneous browser viewers.  Each costs one socket on its server instance
// (WEB_MAX_SOCKETS bounds that side) plus ~39KB/frame of send work; four is
// generous for a hand-held thermal camera without inviting a crowd.
#define WEB_CMD_MAX_CLIENTS 4


//
// Web Command types
//
typedef struct {
	httpd_handle_t hd;
	int fd;                    // < 0 = free slot
	bool first_frame_logged;
} ws_client_t;


//
// Web Command variables
//
static const char* TAG = "web_cmd";

// Serialises access to the client table and to outbound sends.  rsp_task sends
// image frames and command responses while the server tasks service inbound
// frames and connection teardown.
static SemaphoreHandle_t web_mutex = NULL;

// Serialises the shared command parser.  The HTTP and HTTPS instances run on
// separate server tasks, so two clients' commands could otherwise interleave
// mid-parse.
static SemaphoreHandle_t cmd_mutex = NULL;

static ws_client_t clients[WEB_CMD_MAX_CLIENTS];


//
// Web Command forward declarations
//
static esp_err_t ws_handler(httpd_req_t* req);
static int find_client(int fd);
static bool add_client(httpd_handle_t hd, int fd);
static void remove_client_locked(int idx);
static void log_ws_peer(httpd_handle_t hd, int fd);


//
// Web Command API
//
void web_cmd_init()
{
	int i;

	if (web_mutex == NULL) {
		web_mutex = xSemaphoreCreateMutex();
	}
	if (cmd_mutex == NULL) {
		cmd_mutex = xSemaphoreCreateMutex();
	}
	for (i = 0; i < WEB_CMD_MAX_CLIENTS; i++) {
		clients[i].hd = NULL;
		clients[i].fd = -1;
		clients[i].first_frame_logged = false;
	}
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
	bool up = false;
	int i;

	// rsp_task polls the client state from the moment it starts, which can be
	// before the web server task has run, so this must be safe pre-init
	if (web_mutex == NULL) return false;

	xSemaphoreTake(web_mutex, portMAX_DELAY);
	for (i = 0; i < WEB_CMD_MAX_CLIENTS; i++) {
		if (clients[i].fd >= 0) {
			up = true;
			break;
		}
	}
	xSemaphoreGive(web_mutex);

	return up;
}


/**
 * Broadcast a frame to every connected browser.  A client whose socket fails is
 * dropped on the spot - browsers reconnect by themselves.  Returns the payload
 * length if at least one client received it, else -1.
 */
static int broadcast(httpd_ws_frame_t* frame)
{
	bool any_left = false;
	int i;
	int sent = 0;

	if (web_mutex == NULL) return -1;

	xSemaphoreTake(web_mutex, portMAX_DELAY);

	for (i = 0; i < WEB_CMD_MAX_CLIENTS; i++) {
		if (clients[i].fd < 0) continue;

		if (httpd_ws_send_frame_async(clients[i].hd, clients[i].fd, frame) == ESP_OK) {
			// Mark the socket as freshly used.  httpd's LRU purge only counts a
			// socket as active when it RECEIVES a request, and a streaming
			// WebSocket is almost pure outbound - without this, every
			// frame-carrying socket looked idle and was the first thing evicted
			// whenever the browser opened a new HTTP connection.
			httpd_sess_update_lru_counter(clients[i].hd, clients[i].fd);
			if ((frame->type == HTTPD_WS_TYPE_BINARY) && !clients[i].first_frame_logged) {
				clients[i].first_frame_logged = true;
				ESP_LOGI(TAG, "First image frame sent to ws client %d (%d bytes)",
				         clients[i].fd, (int) frame->len);
			}
			sent++;
		} else {
			ESP_LOGI(TAG, "ws client %d dropped (send failed)", clients[i].fd);
			remove_client_locked(i);
		}
	}

	for (i = 0; i < WEB_CMD_MAX_CLIENTS; i++) {
		if (clients[i].fd >= 0) any_left = true;
	}

	xSemaphoreGive(web_mutex);

	// Release outside the table lock; client_if has its own mutex
	if (!any_left && (sent == 0)) {
		client_if_release(CLIENT_IF_WS);
	}

	return (sent > 0) ? (int) frame->len : -1;
}


int web_cmd_send(char* buf, int len)
{
	httpd_ws_frame_t frame;

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

	memset(&frame, 0, sizeof(frame));
	frame.type    = HTTPD_WS_TYPE_TEXT;
	frame.payload = (uint8_t*) buf;
	frame.len     = len;
	frame.final   = true;

	return broadcast(&frame);
}


int web_cmd_send_binary(char* buf, int len)
{
	httpd_ws_frame_t frame;

	if (len <= 0) return 0;

	memset(&frame, 0, sizeof(frame));
	frame.type    = HTTPD_WS_TYPE_BINARY;
	frame.payload = (uint8_t*) buf;
	frame.len     = len;
	frame.final   = true;

	return broadcast(&frame);
}


void web_cmd_close_fn(httpd_handle_t hd, int sockfd)
{
	bool any_left = false;
	int i, idx;

	(void) hd;

	xSemaphoreTake(web_mutex, portMAX_DELAY);
	idx = find_client(sockfd);
	if (idx >= 0) {
		ESP_LOGI(TAG, "ws client %d closed", sockfd);
		remove_client_locked(idx);
	}
	for (i = 0; i < WEB_CMD_MAX_CLIENTS; i++) {
		if (clients[i].fd >= 0) any_left = true;
	}
	xSemaphoreGive(web_mutex);

	if ((idx >= 0) && !any_left) {
		client_if_release(CLIENT_IF_WS);
	}

	// Perform the default teardown the server would otherwise have done
	close(sockfd);
}


//
// Web Command internal functions
//

/**
 * Index of fd in the table, -1 if absent.  Call with web_mutex held.
 */
static int find_client(int fd)
{
	int i;

	for (i = 0; i < WEB_CMD_MAX_CLIENTS; i++) {
		if (clients[i].fd == fd) return i;
	}
	return -1;
}


static void remove_client_locked(int idx)
{
	clients[idx].hd = NULL;
	clients[idx].fd = -1;
	clients[idx].first_frame_logged = false;
}


/**
 * Admit a new browser.  Fails when the legacy TCP client holds the command
 * session or when the table is full.
 */
static bool add_client(httpd_handle_t hd, int fd)
{
	bool first = true;
	bool ok = false;
	int i;

	xSemaphoreTake(web_mutex, portMAX_DELAY);

	for (i = 0; i < WEB_CMD_MAX_CLIENTS; i++) {
		if (clients[i].fd >= 0) first = false;
	}

	// The whole browser population holds ONE client_if claim; the arbitration
	// that matters is browsers-versus-TCP, not browser-versus-browser
	if (!first || client_if_claim(CLIENT_IF_WS)) {
		for (i = 0; i < WEB_CMD_MAX_CLIENTS; i++) {
			if (clients[i].fd < 0) {
				clients[i].hd = hd;
				clients[i].fd = fd;
				clients[i].first_frame_logged = false;
				ok = true;
				break;
			}
		}
		if (!ok && first) {
			client_if_release(CLIENT_IF_WS);
		}
	}

	xSemaphoreGive(web_mutex);

	if (ok) {
		if (first) {
			// One shared parser for all browsers; reset it as the population starts
			init_command_processor();
		}
		log_ws_peer(hd, fd);
	} else {
		ESP_LOGW(TAG, "Refusing ws client %d (%s)", fd,
		         first ? "TCP client holds the session" : "viewer limit reached");
	}
	return ok;
}


/**
 * Log which peer just joined, and on which transport
 */
static void log_ws_peer(httpd_handle_t hd, int fd)
{
	const char* proto = web_handle_is_https(hd) ? "wss" : "ws";
	struct sockaddr_storage addr;
	socklen_t alen = sizeof(addr);

	if (getpeername(fd, (struct sockaddr*) &addr, &alen) == 0) {
		if (addr.ss_family == AF_INET) {
			struct sockaddr_in* a4 = (struct sockaddr_in*) &addr;
			ESP_LOGI(TAG, "%s client %d connected from %s", proto, fd,
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
				ESP_LOGI(TAG, "%s client %d connected from %d.%d.%d.%d",
				         proto, fd, b[12], b[13], b[14], b[15]);
				return;
			}
		}
#endif
	}
	ESP_LOGI(TAG, "%s client %d connected", proto, fd);
}


static esp_err_t ws_handler(httpd_req_t* req)
{
	char delim;
	esp_err_t ret;
	httpd_ws_frame_t frame;
	int fd, idx;
	static uint8_t rx_buf[WEB_CMD_MAX_RX_LEN + 1];

	fd = httpd_req_to_sockfd(req);

	// IDF 4.4 invoked this handler for the handshake GET; IDF 5 completes the
	// handshake internally and never calls it for the upgrade (httpd_uri.c: "If
	// the request is websocket handshake, then do not call the uri->handler"),
	// so admission normally happens at the first data frame below.
	if (req->method == HTTP_GET) {
		if (!add_client(req->handle, fd)) {
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
			                    "Camera is in use by another client");
			return ESP_FAIL;
		}
		return ESP_OK;
	}

	xSemaphoreTake(web_mutex, portMAX_DELAY);
	idx = find_client(fd);
	xSemaphoreGive(web_mutex);

	if (idx < 0) {
		if (!add_client(req->handle, fd)) {
			// Only the legacy TCP protocol or a full house refuses a browser now;
			// say so on the socket before closing it (the UI shows it as a toast)
			static const char busy_msg[] =
				"{\"cam_info\":{\"info_value\":0,"
				"\"info_string\":\"Camera is in use by another client\"}}";
			httpd_ws_frame_t busy;

			memset(&busy, 0, sizeof(busy));
			busy.type = HTTPD_WS_TYPE_TEXT;
			busy.payload = (uint8_t*) busy_msg;
			busy.len = sizeof(busy_msg) - 1;
			busy.final = true;
			(void) httpd_ws_send_frame(req, &busy);
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
		// Remove from the table only - the server owns the socket teardown on
		// this path (close_fn handles the case where the TCP side just dies)
		bool any_left = false;
		int i;

		xSemaphoreTake(web_mutex, portMAX_DELAY);
		idx = find_client(fd);
		if (idx >= 0) {
			ESP_LOGI(TAG, "ws client %d said goodbye", fd);
			remove_client_locked(idx);
		}
		for (i = 0; i < WEB_CMD_MAX_CLIENTS; i++) {
			if (clients[i].fd >= 0) any_left = true;
		}
		xSemaphoreGive(web_mutex);
		if ((idx >= 0) && !any_left) {
			client_if_release(CLIENT_IF_WS);
		}
		return ESP_OK;
	}

	if ((frame.type != HTTPD_WS_TYPE_TEXT) || (frame.len == 0)) {
		// PING/PONG are handled internally by the server
		return ESP_OK;
	}

	// Re-frame into the sentinel-delimited form the shared parser expects and let
	// the existing command processor do all the work.  The parser is one shared
	// state machine and this handler runs on both server instances' tasks, so
	// the push-and-drain must be atomic per command.
	xSemaphoreTake(cmd_mutex, portMAX_DELAY);
	delim = CMD_JSON_STRING_START;
	push_rx_data(&delim, 1);
	push_rx_data((char*) rx_buf, frame.len);
	delim = CMD_JSON_STRING_STOP;
	push_rx_data(&delim, 1);

	while (process_rx_data()) {}
	xSemaphoreGive(cmd_mutex);

	return ESP_OK;
}
