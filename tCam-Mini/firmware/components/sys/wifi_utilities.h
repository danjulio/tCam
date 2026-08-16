/*
 * WiFi related utilities
 *
 * Contains functions to initialize and query the wifi interface.
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
#ifndef WIFI_UTILITIES_H
#define WIFI_UTILITIES_H

#include <stdbool.h>
#include <stdint.h>
#include "net_utilities.h"

//
// WiFi Utilities Constants
//

// Maximum attempts to reconnect to an AP in client mode before starting to wait
#define WIFI_FAST_RECONNECT_ATTEMPTS  10

// Stations allowed to associate with the camera's SoftAP.  This was 1, which meant
// a phone already on the camera silently locked everyone else out - including the
// operator's laptop.  Command sessions are still arbitrated to one at a time by
// client_if, but simply being able to load the page should not be exclusive.
#define WIFI_AP_MAX_CONN              4

// Consecutive failed attempts to reach the configured network before the camera
// raises its own recovery access point (station keeps retrying underneath).
// Each failed attempt takes several seconds of scanning, so this lands at
// roughly 45-90 seconds - late enough not to trigger on a router rebooting,
// and if it does, no harm: the AP dissolves the moment the station connects.
#define WIFI_FALLBACK_AP_FAILS        12


//
// WiFi Utilities API
//
bool wifi_init();
bool wifi_reinit();
bool wifi_is_connected();
net_info_t* wifi_get_info();

/**
 * True when the WiFi interface is acting as an access point rather than as a
 * station on someone else's network.
 */
bool wifi_is_ap_mode();

/**
 * True while the recovery access point is up because the configured network is
 * unreachable.  The camera behaves as an AP (captive portal, AP address) even
 * though its stored configuration says station mode.
 */
bool wifi_is_fallback_active();

#endif /* WIFI_UTILITIES_H */
