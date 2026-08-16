/*
 * Set Time GUI screen related functions, callbacks and event handlers
 *
 * Copyright 2020 Dan Julio
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
#ifndef GUI_SCREEN_TIME_H
#define GUI_SCREEN_TIME_H

#include "lvgl/lvgl.h"


//
// Set Time GUI Screen Constants
//

// Screen Title Label
#define T_TITLE_LBL_X        240
#define T_TITLE_LBL_Y        5

// Button Matrix
#define T_BTN_MATRIX_X   90
#define T_BTN_MATRIX_Y   120
#define T_BTN_MATRIX_W   300
#define T_BTN_MATRIX_H   160


//
// Set Time GUI Screen API
//
lv_obj_t* gui_screen_time_create();
void gui_screen_time_set_active(bool en);

#endif /* GUI_SCREEN_TIME_H */