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


//
// WiFi Utilities API
//
bool wifi_init();
bool wifi_reinit();
bool wifi_is_connected();
net_info_t* wifi_get_info();

#endif /* WIFI_UTILITIES_H */
