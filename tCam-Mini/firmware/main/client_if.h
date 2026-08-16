/*
 * Client Interface Arbitration
 *
 * The camera supports one active command/response client at a time.  Historically
 * that was always the raw TCP socket on CMD_PORT used by the desktop and mobile
 * applications.  A browser talking to the on-camera web server over a WebSocket is
 * an equally valid client, and both speak the identical json protocol, so this
 * module owns which transport currently holds the session and dispatches outbound
 * responses to it.
 *
 * Arbitration is first-come-first-served.  A second client is refused rather than
 * being allowed to interleave into the single shared receive/response buffer pair
 * owned by cmd_utilities.
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
#ifndef CLIENT_IF_H
#define CLIENT_IF_H

#include <stdbool.h>
#include <stdint.h>


//
// Client Interface Constants
//
#define CLIENT_IF_NONE 0
#define CLIENT_IF_TCP  1
#define CLIENT_IF_WS   2


//
// Client Interface API
//

/**
 * One-time initialisation.  Must be called before any task attempts to claim.
 */
void client_if_init();

/**
 * Attempt to become the active client.  Returns false if a different transport
 * already holds the session.  Re-claiming from the transport that already holds
 * the session succeeds and is a no-op.
 */
bool client_if_claim(int kind);

/**
 * Release the session.  Ignored if the caller is not the current holder.
 */
void client_if_release(int kind);

/**
 * True when some transport currently holds the session.
 */
bool client_if_connected();

/**
 * Return the transport currently holding the session (CLIENT_IF_* above).
 */
int client_if_active();

/**
 * Send a fully formed response (including its 0x02/0x03 delimiters) to the
 * active client.  Returns the number of bytes accepted, or -1 on error.  A
 * send error releases the session so the transport can be re-established.
 */
int client_if_send(char* buf, int len);

#endif /* CLIENT_IF_H */
