/*
 * Main GUI screen related functions, callbacks and event handlers
 *
 * Copyright 2020-2021 Dan Julio
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
#ifndef GUI_SCREEN_MAIN_H
#define GUI_SCREEN_MAIN_H

#include <stdint.h>
#include <stdbool.h>
#include "lvgl/lvgl.h"

//
// Main Screen Constants
//

// Header text
#define MAIN_SSID_LBL_X         60
#define MAIN_SSID_LBL_Y         7
#define MAIN_SSID_LBL_W         80

#define MAIN_TIME_LBL_X         260
#define MAIN_TIME_LBL_Y         7
#define MAIN_TIME_LBL_W         45

#define MAIN_BATT_LBL_X         310
#define MAIN_BATT_LBL_Y         7
#define MAIN_BATT_LBL_W         50

#define MAIN_SDCARD_LBL_X       360
#define MAIN_SDCARD_LBL_Y       7
#define MAIN_SDCARD_LBL_W       20

// Current Colormap canvas
#define MAIN_CMAP_X             5
#define MAIN_CMAP_Y             10
#define MAIN_CMAP_CANVAS_WIDTH  50
#define MAIN_CMAP_CANVAS_HEIGHT 300
#define MAIN_CMAP_CANVAS_PIXELS (MAIN_CMAP_CANVAS_WIDTH*MAIN_CMAP_CANVAS_HEIGHT)
#define MAIN_CMAP_PALETTE_X1    5
#define MAIN_CMAP_PALETTE_X2    25
#define MAIN_CMAP_MARKER_X1     30
#define MAIN_CMAP_MARKER_X2     34
#define MAIN_CMAP_MARKER_H      6

// Lepton display area
#define MAIN_IMG_X              60
#define MAIN_IMG_Y              30

// Recording Status Label
#define MAIN_REC_LBL_X          410
#define MAIN_REC_LBL_Y          7

// Controls
#define MAIN_MODE_SW_X          400
#define MAIN_MODE_SW_Y          40
#define MAIN_MODE_SW_W          70
#define MAIN_MODE_SW_H          30

#define MAIN_MODE_SW_LBL_X      401
#define MAIN_MODE_SW_LBL_Y      75

#define MAIN_BROWSE_BTN_X       390
#define MAIN_BROWSE_BTN_Y       110
#define MAIN_BROWSE_BTN_W       80
#define MAIN_BROWSE_BTN_H       60

#define MAIN_SETUP_BTN_X        390
#define MAIN_SETUP_BTN_Y        190
#define MAIN_SETUP_BTN_W        80
#define MAIN_SETUP_BTN_H        40

#define MAIN_PWROFF_BTN_X       430
#define MAIN_PWROFF_BTN_Y       275
#define MAIN_PWROFF_BTN_W       40
#define MAIN_PWROFF_BTN_H       35

#define MAIN_FFC_BTN_X          340
#define MAIN_FFC_BTN_Y          275
#define MAIN_FFC_BTN_W          40
#define MAIN_FFC_BTN_H          35

#define MAIN_AGC_BTN_X          110
#define MAIN_AGC_BTN_Y          275
#define MAIN_AGC_BTN_W          40
#define MAIN_AGC_BTN_H          35

#define MAIN_RNGMODE_BTN_X      60
#define MAIN_RNGMODE_BTN_Y      275
#define MAIN_RNGMODE_BTN_W      40
#define MAIN_RNGMODE_BTN_H      35

// Spot Temperature Label
#define MAIN_SPOT_TEMP_LBL_X    220
#define MAIN_SPOT_TEMP_LBL_Y    270

// Bottom Information Area Label
#define MAIN_INFO_LBL_X         160
#define MAIN_INFO_LBL_Y         295


//
// Image state
//
#define IMG_STATE_PICTURE 0
#define IMG_STATE_REC_ON  1
#define IMG_STATE_REC_OFF 2


//
// Palette background color
//
#define MAIN_PALETTE_BG_COLOR  LV_COLOR_MAKE(0x30, 0x30, 0x30)



//
// Main GUI Screen API
//
lv_obj_t* gui_screen_main_create();
void gui_screen_main_set_active(bool en);
void gui_screen_main_update_lep_image(int n);
void gui_screen_main_set_image_state(int st);
void gui_screen_display_message(char * msg, int disp_secs);
void gui_screen_agc_updated();

#endif /* GUI_SCREEN_MAIN_H */