/*
 * Web Server Task
 *
 * Serves the on-camera user interface.  The camera becomes self-contained: joining
 * its access point (or browsing to it on the local network) is enough to get a full
 * viewer and configuration UI with no application to install.
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
#ifndef WEB_TASK_H
#define WEB_TASK_H

#include <stdbool.h>


//
// Web Task Constants
//

// Port the UI is served on.  80 so a bare address works with no port suffix.
//
// Plain HTTP only.  An HTTPS instance (with an on-device certificate authority)
// shipped in 6.6-6.8 and was removed in 6.9: esp_https_server runs every TLS
// handshake synchronously on the same task that carries the WebSocket stream,
// so each new connection - including every probe from a device that does not
// trust the certificate - froze the video for most of a second.  On a LAN
// camera the encryption bought nothing worth that, and the plain-HTTP stream
// is smooth.
#define WEB_PORT 80

// Define to restore the per-request httpd log output that normal browser
// behavior fills the log with.  Needed only when debugging the server itself.
//#define WEB_VERBOSE_NET_LOGS

// Stack for the server task.  Command handling runs cJSON in this context, so this
// is well above the esp_http_server default.
#define WEB_TASK_STACK_SIZE 6144

// Sockets the server accepts.  Also sizes the array passed to
// httpd_get_client_list(), which requires at least max_open_sockets entries.
//
// Budget carefully: the httpd instance costs max_open_sockets PLUS three
// internal sockets (listen, and a UDP pair for control messages), and
// CONFIG_LWIP_MAX_SOCKETS is capped at 16 on this target.  In access point mode
// the total is the server, the legacy command port's listen and client sockets,
// and the captive portal's UDP socket:
//
//   (N+3) + 2 + 1  <= 16   =>  N <= 10
//
// With the HTTPS instance gone the pool has room to spare; 6 covers four
// simultaneous WebSocket viewers (web_cmd's limit) plus page and API fetches
// without letting a rogue client tie up the whole socket table.
#define WEB_MAX_SOCKETS 6

// URI handler slots per server instance.  esp_http_server defaults to 8 and this
// server registers nine handlers, the WebSocket endpoint among them - and it
// registers that one last, so the endpoint that carries the entire image stream
// was the one that silently failed to install.  Requests to /ws then fell
// through to the 404 handler, which redirects, so the browser's WebSocket saw a
// 302 instead of a 101 and never opened.  Sized with headroom; each slot costs
// one pointer.
//
// Adding a route means checking this number.  register_handlers() now reports a
// registration failure rather than leaving it to be inferred from behaviour.
#define WEB_MAX_URI_HANDLERS 12


//
// Web Task API
//

/**
 * Start the web server.  Blocks briefly waiting for the network interface to come
 * up, then runs until the system is reset.
 */
void web_task();

/**
 * True while an OTA upload is in progress (used to suppress streaming).
 */
bool web_ota_in_progress();

#endif /* WEB_TASK_H */
