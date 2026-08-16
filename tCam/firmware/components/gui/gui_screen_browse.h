/*
 * Browse Files GUI screen related functions, callbacks and event handlers
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
#ifndef GUI_SCREEN_BROWSE_H
#define GUI_SCREEN_BROWSE_H

#include "lvgl/lvgl.h"



//
// Browse Files Constants
//

// Screen Title Label
#define B_TITLE_LBL_X        240
#define B_TITLE_LBL_Y        5

// Directory List Label
#define B_DIR_LBL_X          10
#define B_DIR_LBL_Y          40

// Exit button
#define B_EXIT_BTN_X         430
#define B_EXIT_BTN_Y         10
#define B_EXIT_BTN_W         40
#define B_EXIT_BTN_H         35

// Directory List Table dimensions
#define B_DIR_TBL_PAGE_X     10
#define B_DIR_TBL_PAGE_Y     60
#define B_DIR_TBL_PAGE_H     215

// File List Label
#define B_FILE_LBL_X         190
#define B_FILE_LBL_Y         40

// File List Table dimensions
#define B_FILE_TBL_PAGE_X    190
#define B_FILE_TBL_PAGE_Y    60
#define B_FILE_TBL_PAGE_H    255

// Shared table/label width
#define B_TBL_PAGE_W         170

// Filesystem Information Label
#define B_FS_INFO_LBL_X      370
#define B_FS_INFO_LBL_Y      60
#define B_FS_INFO_LBL_W      120

// View File Button
#define B_VIEW_BTN_X         380
#define B_VIEW_BTN_Y         80
#define B_VIEW_BTN_W         80
#define B_VIEW_BTN_H         60

// Delete File/Directory Button
#define B_DEL_BTN_X          380
#define B_DEL_BTN_Y          165
#define B_DEL_BTN_W          80
#define B_DEL_BTN_H          50

// Format Button
#define B_FMT_BTN_X          380
#define B_FMT_BTN_Y          240
#define B_FMT_BTN_W          80
#define B_FMT_BTN_H          50

// Number of files information label
#define B_NUM_FILE_LBL_X     10
#define B_NUM_FILE_LBL_Y     280
#define B_NUM_FILE_LBL_W     170

// Storage Free information label
#define B_FREE_LBL_X         10
#define B_FREE_LBL_Y         300
#define B_FREE_LBL_W         170



//
// Browse Files GUI Screen API
//
lv_obj_t* gui_screen_browse_create();
void gui_screen_browse_set_active(bool en);
void gui_screen_browse_status_update(lv_task_t * task);
void gui_screen_browse_update_list();
void gui_screen_browse_set_msgbox_btn(uint16_t btn);
void gui_screen_browse_update_after_delete();

#endif /* GUI_SCREEN_BROWSE_H */