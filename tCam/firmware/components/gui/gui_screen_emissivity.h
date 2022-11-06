/*
 * Emissivity Lookup GUI screen related functions, callbacks and event handlers
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
#ifndef GUI_SCREEN_EMISSIVITY_H
#define GUI_SCREEN_EMISSIVITY_H

#include "lvgl/lvgl.h"



//
// Emissivity Lookup Constants
//

// Screen Title Label
#define E_TITLE_LBL_X   240
#define E_TITLE_LBL_Y   5

// Exit button
#define E_EXIT_BTN_X    430
#define E_EXIT_BTN_Y    10
#define E_EXIT_BTN_W    40
#define E_EXIT_BTN_H    35

// Save button
#define E_SAVE_BTN_X    10
#define E_SAVE_BTN_Y    10
#define E_SAVE_BTN_W    40
#define E_SAVE_BTN_H    35

// Table dimensions
#define E_TBL_PAGE_X    0
#define E_TBL_PAGE_Y    80
#define E_TBL_PAGE_W    480
#define E_TBL_PAGE_H    240

//
// Emissivity Lookup GUI Screen API
//
lv_obj_t* gui_screen_emissivity_create();
void gui_screen_emissivity_set_active(bool en);

#endif /* GUI_SCREEN_EMISSIVITY_H */