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
#define WEB_PORT 80

// Define to restore the per-handshake TLS/httpd log output that normal browser
// behavior (aborted handshakes while a certificate warning is on screen) fills
// the log with.  Needed only when debugging the TLS stack itself.
//#define WEB_VERBOSE_NET_LOGS

// Stack for the server task.  Command handling runs cJSON in this context, so this
// is well above the esp_http_server default.
#define WEB_TASK_STACK_SIZE 6144

// Sockets each server instance accepts.  Also sizes the array passed to
// httpd_get_client_list(), which requires at least max_open_sockets entries, so
// both servers are configured from this one value to keep them in step.
//
// Budget carefully: each httpd instance costs max_open_sockets PLUS three
// internal sockets (listen, and a UDP pair for control messages), and
// CONFIG_LWIP_MAX_SOCKETS is capped at 16 on this target.  In access point mode
// the total is two servers, the legacy command port's listen and client sockets,
// and the captive portal's UDP socket:
//
//   (N+3)*2 + 2 + 1  <= 16   =>  N <= 3
//
// At N=4 that came to 17 and would have failed an accept once everything was in
// use.  httpd's own check only validates one instance at a time (N+3 <= 16), so
// it does not catch the overrun.
#define WEB_MAX_SOCKETS 3


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
