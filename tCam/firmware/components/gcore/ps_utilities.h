/*
 * Persistent Storage Module
 *
 * Manage the persistent storage kept in the gCore EFM8 RAM and provide access
 * routines to it.
 *
 * NOTE: It is assumed that only task will access persistent storage at a time.
 * This is done to eliminate the need for mutex protection, that could cause a 
 * dead-lock with another process also accessing a device via I2C.
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
#ifndef PS_UTILITIES_H
#define PS_UTILITIES_H

#include "gui_utilities.h"
#include "lepton_utilities.h"
#include "sys_utilities.h"
#include "wifi_utilities.h"
#include <stdbool.h>
#include <stdint.h>



//
// PS Utilities Constants
//

// PS Size
//  - must be less than contained in gCore's EFM8 RAM
//  - should be fairly small to keep I2C burst length down
#define PS_RAM_SIZE         256
#define PS_RAM_STARTADDR    0

// Base part of the default SSID/Camera name - the last 4 nibbles of the ESP32's
// mac address are appended as ASCII characters
#define PS_DEFAULT_AP_SSID "tCam-"

// Field lengths
#define PS_SSID_MAX_LEN     32
#define PS_PW_MAX_LEN       63



//
// PS Utilities API
//
bool ps_init();
void ps_set_factory_default();
void ps_save_to_flash();
void ps_get_gui_state(gui_state_t* state);
void ps_set_gui_state(const gui_state_t* state);
void ps_get_lep_state(lep_config_t* state);
void ps_set_lep_state(const lep_config_t* state);
void ps_get_wifi_info(wifi_info_t* info);
void ps_set_wifi_info(const wifi_info_t* info);
bool ps_has_new_cam_name(const wifi_info_t* info);

#endif /* PS_UTILITIES_H */