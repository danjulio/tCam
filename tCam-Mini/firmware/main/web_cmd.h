/*
 * Web Command Bridge
 *
 * Bridges a browser WebSocket to the existing json command processor in
 * cmd_utilities.  The camera's command protocol is unchanged - the browser sends
 * and receives exactly the same json objects the desktop application does.  The
 * only difference is framing: the raw TCP transport delimits objects with 0x02 and
 * 0x03 sentinel bytes, whereas a WebSocket already carries message boundaries, so
 * this module adds the delimiters on the way in and strips them on the way out.
 *
 * Reusing the command processor rather than inventing a REST surface means every
 * capability the camera already exposes is immediately available to the web UI,
 * and the two client types cannot drift apart.
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
#ifndef WEB_CMD_H
#define WEB_CMD_H

#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"


//
// Web Command API
//

/**
 * One-time initialisation.
 */
void web_cmd_init();

/**
 * Register the /ws endpoint on a running server.
 */
esp_err_t web_cmd_register(httpd_handle_t server);

/**
 * Called by the server when a socket is torn down so a browser that goes away
 * without a clean close still releases the command session.
 */
void web_cmd_close_fn(httpd_handle_t hd, int sockfd);

/**
 * True while a browser WebSocket holds the command session.
 */
bool web_cmd_connected();

/**
 * Send a fully formed response (including its 0x02/0x03 delimiters, which are
 * stripped here) to the connected browser.  Returns bytes sent or -1 on error.
 * Safe to call from any task.
 */
int web_cmd_send(char* buf, int len);

/**
 * Send a raw binary frame (see build_binary_image in rsp_task for the layout)
 * to the connected browser.  Returns bytes sent or -1 on error.  Safe to call
 * from any task.
 */
int web_cmd_send_binary(char* buf, int len);

#endif /* WEB_CMD_H */
