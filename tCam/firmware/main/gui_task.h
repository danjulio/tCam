/*
 * GUI Task
 *
 * Contains functions to initialize the LittleVGL GUI system and a task
 * to evaluate its display related sub-tasks.  The GUI Task is responsible
 * for all access (updating) of the GUI managed by LittleVGL.
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
 * along with firecam.  If not, see <https://www.gnu.org/licenses/>.
 *
 */
#ifndef GUI_TASK_H
#define GUI_TASK_H

#include <stdbool.h>
#include <stdint.h>


//
// GUI Task Constants
//

// Screen indicies
#define GUI_SCREEN_MAIN            0
#define GUI_SCREEN_SETTINGS        1
#define GUI_SCREEN_NUM_ENTRY       2
#define GUI_SCREEN_EMISSIVITY      3
#define GUI_SCREEN_INFO            4
#define GUI_SCREEN_NETWORK         5
#define GUI_SCREEN_TIME            6
#define GUI_SCREEN_WIFI            7
#define GUI_SCREEN_VIEW            8
#define GUI_SCREEN_BROWSE          9
#define GUI_SCREEN_UPDATE          10
#define GUI_NUM_SCREENS            11


// LVGL evaluation rate (mSec)
#define GUI_EVAL_MSEC              25

//
// GUI Task notifications
//
// From lep_task
#define GUI_NOTIFY_LEP_FRAME_MASK_1         0x00000001
#define GUI_NOTIFY_LEP_FRAME_MASK_2         0x00000002

// From app_task
#define GUI_NOTIFY_RECORD_IMG_MASK          0x00000010
#define GUI_NOTIFY_RECORD_ON_MASK           0x00000020
#define GUI_NOTIFY_RECORD_OFF_MASK          0x00000040
#define GUI_NOTIFY_RECORD_FAIL_MASK         0x00000080
#define GUI_NOTIFY_MESSAGEBOX_MASK          0x00000100
#define GUI_NOTIFY_SDCARD_MISSING_MASK      0x00000200
#define GUI_NOTIFY_FILE_DEL_FILE_DONE_MASK  0x00000400
#define GUI_NOTIFY_FW_UPD_REQ_MB_MASK       0x00001000
#define GUI_NOTIFY_FW_UPD_END_MB_MASK       0x00002000
#define GUI_NOTIFY_FW_UPD_START_MASK        0x00004000
#define GUI_NOTIFY_FW_UPD_STOP_MASK         0x00008000

// From cmd_task
#define GUI_NOTIFY_NEW_AGC_MASK             0x00010000

// From file_task
#define GUI_NOTIFY_FILE_CATALOG_READY_MASK  0x00100000
#define GUI_NOTIFY_FILE_IMAGE_READY_MASK    0x00200000
#define GUI_NOTIFY_FILE_UPDATE_PB_LEN_MASK  0x00400000
#define GUI_NOTIFY_FILE_UPDATE_PB_TS_MASK   0x00800000
#define GUI_NOTIFY_FILE_PB_DONE_MASK        0x01000000

// From gcore_task
#define GUI_NOTIFY_SNAP_BTN_PRESSED_MASK    0x10000000

// Trigger screen dump
#define GUI_NOTIFY_SCREENDUMP_MASK           0x80000000


// Number of samples in fps averaging array
#define GUI_NUM_FPS_SAMPLES        20


//
// GUI Task API
//
void gui_task();
void gui_set_screen(int n);
void gui_set_write_filename(char* name);
void gui_set_msgbox_btn(uint16_t btn);
void gui_set_playback_ts(uint32_t ts);
 

#endif /* GUI_TASK_H */