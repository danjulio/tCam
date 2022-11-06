/*
 * App Task
 *
 * Implement the application logic for firecam.  The program's maestro. 
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
#ifndef APP_TASK_H
#define APP_TASK_H

#include <stdbool.h>
#include <stdint.h>



//
// App Task constants
//

// App task evaluation interval
#define APP_EVAL_MSEC 100

// App Task notifications
#define APP_NOTIFY_SHUTDOWN_MASK            0x00000001
#define APP_NOTIFY_NEW_WIFI_MASK            0x00000002

#define APP_NOTIFY_SDCARD_PRESENT_MASK      0x00000004
#define APP_NOTIFY_SDCARD_MISSING_MASK      0x00000008
#define APP_NOTIFY_FILESYSTEM_READY_MASK    0x00000010
#define APP_NOTIFY_FILE_CATALOG_READY_MASK  0x00000020

#define APP_NOTIFY_CMD_TAKE_PICTURE_MASK    0x00000040
#define APP_NOTIFY_CMD_START_RECORD_MASK    0x00000080
#define APP_NOTIFY_CMD_STOP_RECORD_MASK     0x00000100
#define APP_NOTIFY_CMD_DEL_FILE_MASK        0x00000200
#define APP_NOTIFY_CMD_DEL_DIR_MASK         0x00000400
#define APP_NOTIFY_CMD_FORMAT_MASK          0x00000800
#define APP_NOTIFY_CMD_DEL_SUCCESS_MASK     0x00001000
#define APP_NOTIFY_CMD_DEL_FAIL_MASK        0x00002000

#define APP_NOTIFY_GUI_TAKE_PICTURE_MASK    0x00004000
#define APP_NOTIFY_GUI_START_RECORD_MASK    0x00008000
#define APP_NOTIFY_GUI_STOP_RECORD_MASK     0x00010000
#define APP_NOTIFY_GUI_DEL_FILE_MASK        0x00020000
#define APP_NOTIFY_GUI_DEL_DIR_MASK         0x00040000
#define APP_NOTIFY_GUI_FORMAT_MASK          0x00080000
#define APP_NOTIFY_GUI_DEL_SUCCESS_MASK     0x00100000
#define APP_NOTIFY_GUI_DEL_FAIL_MASK        0x00200000

#define APP_NOTIFY_RECORD_IMG_DONE_MASK     0x00400000
#define APP_NOTIFY_RECORD_START_MASK        0x00800000
#define APP_NOTIFY_RECORD_STOP_MASK         0x01000000
#define APP_NOTIFY_RECORD_FAIL_MASK         0x02000000
#define APP_NOTIFY_PB_CMD_FAIL_MASK         0x04000000
#define APP_NOTIFY_PB_GUI_FAIL_MASK         0x08000000

#define APP_NOTIFY_FW_UPD_REQ               0x10000000
#define APP_NOTIFY_FW_UPD_PROCESS           0x20000000
#define APP_NOTIFY_FW_UPD_DONE              0x40000000
#define APP_NOTIFY_FW_UPD_FAIL              0x80000000



//
// App Task API
//
void app_task();
void app_set_write_filename(char* name);
void app_set_cmd_record_parameters(uint32_t delay_ms, uint32_t num_frames);
 
#endif /* APP_TASK_H */