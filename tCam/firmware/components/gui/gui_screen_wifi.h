/*
 * Wifi Configuration GUI screen related functions, callbacks and event handlers
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
#ifndef GUI_SCREEN_WIFI_H
#define GUI_SCREEN_WIFI_H

#include "lvgl/lvgl.h"


//
// WiFi Constants
//

// Screen Title Label
#define W_TITLE_LBL_X    240
#define W_TITLE_LBL_Y    5

// Enable checkbox
#define W_EN_CB_X        5
#define W_EN_CB_Y        35
#define W_EN_CB_W        40

// Access Point Enable checkbox
#define W_AP_EN_CB_X     120
#define W_AP_EN_CB_Y     35
#define W_AP_EN_CB_W     40

// Text areas
#define W_SSID_LBL_X     10
#define W_SSID_LBL_Y     85

#define W_SSID_TA_X      50
#define W_SSID_TA_Y      75
#define W_SSID_TA_W      190

#define W_PW_LBL_X       10
#define W_PW_LBL_Y       125

#define W_PW_TA_X        50
#define W_PW_TA_Y        115
#define W_PW_TA_W        190

// Buttons
#define W_SCAN_BTN_X     250
#define W_SCAN_BTN_Y     40
#define W_SCAN_BTN_W     30
#define W_SCAN_BTN_H     30

#define W_PW_BTN_X       250
#define W_PW_BTN_Y       120
#define W_PW_BTN_W       30
#define W_PW_BTN_H       30

// SSID Scan Preloader (X/Y computed from Wifi SSID List Table dimensions)
#define W_SCAN_PL_W      30
#define W_SCAN_PL_H      30

// Wifi SSID List Table dimensions
#define W_SSID_TBL_X     290
#define W_SSID_TBL_Y     40
#define W_SSID_TBL_W     180
#define W_SSID_TBL_H     120



//
// Set WiFi GUI Screen API
//
lv_obj_t* gui_screen_wifi_create();
void gui_screen_wifi_set_active(bool en);

#endif /* GUI_SCREEN_WIFI_H */
