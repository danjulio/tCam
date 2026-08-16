/*
 * Camera Information GUI screen related functions, callbacks and event handlers
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
#ifndef GUI_SCREEN_INFO_H
#define GUI_SCREEN_INFO_H

#include "lvgl/lvgl.h"


//
// Camera Information GUI Screen Constants
//

// Screen Title Label
#define I_TITLE_LBL_X        240
#define I_TITLE_LBL_Y        5

// Exit button
#define I_EXIT_BTN_X         430
#define I_EXIT_BTN_Y         10
#define I_EXIT_BTN_W         40
#define I_EXIT_BTN_H         35

// Information text area
#define I_INFO_LBL_X         10
#define I_INFO_LBL_Y         50

// Title label press detection for factory reset
#define I_PRESS_DET_SECS     5
#define I_PRESS_DET_NUM      5



//
// Camera Information GUI Screen API
//
lv_obj_t* gui_screen_info_create();
void gui_screen_info_set_active(bool en);
void gui_screen_info_set_msgbox_btn(uint16_t btn);

#endif /* GUI_SCREEN_INFO_H */