/*
 * BLE Finder Beacon
 *
 * Advertises the camera over Bluetooth LE so the hosted tCam Finder page
 * (docs/ in the repository, served from any HTTPS host) can discover the
 * camera's current network address via Web Bluetooth and open its web
 * interface — solving the "the camera's IP changed and my saved shortcut is
 * dead" problem without any cloud backend: the phone asks the camera itself.
 *
 * One GATT service with a single read characteristic returning a small json
 * object: {"name":"tCam-Mini-XXXX","ip":"a.b.c.d","mode":"sta"|"ap"}.  The
 * json is rebuilt on every read, so it always reports the address the camera
 * holds right now, on whichever network it roamed to.
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
#ifndef BLE_BEACON_H
#define BLE_BEACON_H

#include <stdbool.h>

/**
 * Start the BLE beacon.  Call once, after persistent storage and the network
 * interface are initialized (the beacon reports their data).  Failure is not
 * fatal: the camera works without Bluetooth, the finder page just cannot see
 * it - so this logs and returns rather than raising a fault.
 */
bool ble_beacon_init();

#endif /* BLE_BEACON_H */
