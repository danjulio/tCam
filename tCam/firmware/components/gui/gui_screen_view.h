/*
 * View Files GUI screen related functions, callbacks and event handlers
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
#ifndef GUI_SCREEN_VIEW_H
#define GUI_SCREEN_VIEW_H

#include "lvgl/lvgl.h"



//
// View Files Constants
//

// Header information label
#define VIEW_HEADER_LBL_X        60
#define VIEW_HEADER_LBL_Y        7

// Exit button
#define VIEW_EXIT_BTN_X         430
#define VIEW_EXIT_BTN_Y         10
#define VIEW_EXIT_BTN_W         40
#define VIEW_EXIT_BTN_H         35

// Current Colormap canvas
#define VIEW_CMAP_X             5
#define VIEW_CMAP_Y             10
#define VIEW_CMAP_CANVAS_WIDTH  50
#define VIEW_CMAP_CANVAS_HEIGHT 300
#define VIEW_CMAP_CANVAS_PIXELS (CMAP_CANVAS_WIDTH*CMAP_CANVAS_HEIGHT)
#define VIEW_CMAP_PALETTE_X1    5
#define VIEW_CMAP_PALETTE_X2    25
#define VIEW_CMAP_MARKER_X1     30
#define VIEW_CMAP_MARKER_X2     34
#define VIEW_CMAP_MARKER_H      6

// Lepton display area
#define VIEW_IMG_X              60
#define VIEW_IMG_Y              30

// Controls
#define VIEW_BROWSE_BTN_X       390
#define VIEW_BROWSE_BTN_Y       70
#define VIEW_BROWSE_BTN_W       80
#define VIEW_BROWSE_BTN_H       60

#define VIEW_DELETE_BTN_X       390
#define VIEW_DELETE_BTN_Y       170
#define VIEW_DELETE_BTN_W       80
#define VIEW_DELETE_BTN_H       50

#define VIEW_RNG_MODE_BTN_X     60
#define VIEW_RNG_MODE_BTN_Y     275
#define VIEW_RNG_MODE_BTN_W     40
#define VIEW_RNG_MODE_BTN_H     35

#define VIEW_PLAY_BTN_X         110
#define VIEW_PLAY_BTN_Y         275
#define VIEW_PLAY_BTN_W         40
#define VIEW_PLAY_BTN_H         35

#define VIEW_PREV_BTN_X         370
#define VIEW_PREV_BTN_Y         275
#define VIEW_PREV_BTN_W         40
#define VIEW_PREV_BTN_H         35

#define VIEW_NEXT_BTN_X         430
#define VIEW_NEXT_BTN_Y         275
#define VIEW_NEXT_BTN_W         40
#define VIEW_NEXT_BTN_H         35

// Spot Temperature Label
#define VIEW_SPOT_TEMP_LBL_X    160
#define VIEW_SPOT_TEMP_LBL_Y    270
#define VIEW_SPOT_TEMP_LBL_W    120

// Video Playback Information Area Label
#define VIEW_INFO_LBL_X         160
#define VIEW_INFO_LBL_Y         295



// Playback update state items
#define VIEW_PB_UPD_LEN          0
#define VIEW_PB_UPD_POS          1
#define VIEW_PB_UPD_STATE_DONE   2


//
// Palette background color
//
#define VIEW_PALETTE_BG_COLOR  LV_COLOR_MAKE(0x30, 0x30, 0x30)



//
// View Files GUI Screen API
//
lv_obj_t* gui_screen_view_create();
void gui_screen_view_set_active(bool en);
void gui_screen_view_set_file_info(int abs_file_index);
void gui_screen_view_set_playback_state(int st, uint32_t ts);
void gui_screen_view_state_init();
void gui_screen_view_update_image();
void gui_screen_view_set_msgbox_btn(uint16_t btn);
void gui_screen_view_update_after_delete();

#endif /* GUI_SCREEN_VIEW_H */