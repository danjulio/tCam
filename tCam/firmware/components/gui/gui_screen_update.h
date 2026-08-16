/*
 * FW Update GUI screen related functions, callbacks and event handlers
 *
 * Copyright 2021 Dan Julio
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
#ifndef GUI_SCREEN_UPDATE_H
#define GUI_SCREEN_UPDATE_H

#include "lvgl/lvgl.h"



//
// Update FW Constants
//

// Header information label
#define UPDATE_HEADER_LBL_X       240
#define UPDATE_HEADER_LBL_Y       7

// Exit button
#define UPDATE_EXIT_BTN_X         430
#define UPDATE_EXIT_BTN_Y         10
#define UPDATE_EXIT_BTN_W         40
#define UPDATE_EXIT_BTN_H         35

// Version label
#define UPDATE_VER_LBL_X          90
#define UPDATE_VER_LBL_Y          120

// Progress bar
#define UPDATE_PB_X               90
#define UPDATE_PB_Y               150
#define UPDATE_PB_W               300
#define UPDATE_PB_H               20

// Progress percentage
#define UPDATE_PERCENT_X          90
#define UPDATE_PERCENT_Y          180

// Progress bar update rate (mSec)
#define UPDATE_PROGRESS_MSEC      250


//
// Update FW GUI Screen API
//
lv_obj_t* gui_screen_update_create();
void gui_screen_update_set_active(bool en);
void gui_screen_update_set_msgbox_btn(uint16_t btn);

#endif /* GUI_SCREEN_UPDATE_H */