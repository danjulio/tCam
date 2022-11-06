/*
 * File Task
 *
 * Handle the SD Card and manage writing files for app_task.  Allows file writing time
 * to vary (it increases as the number of files or directories in a directory has to be
 * traversed before creating a new item).
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
#ifndef FILE_TASK_H
#define FILE_TASK_H

#include <stdint.h>
#include <stdbool.h>


//
// File Task Constants
//

// File Task notifications
#define FILE_NOTIFY_STORE_IMAGE_MASK      0x00000001
#define FILE_NOTIFY_START_RECORDING_MASK  0x00000002
#define FILE_NOTIFY_STOP_RECORDING_MASK   0x00000004

#define FILE_NOTIFY_LEP_FRAME_MASK_1      0x00000010
#define FILE_NOTIFY_LEP_FRAME_MASK_2      0x00000020

#define FILE_NOTIFY_CMD_GET_CATALOG_MASK  0x00000100
#define FILE_NOTIFY_CMD_GET_IMAGE_MASK    0x00000200
#define FILE_NOTIFY_CMD_GET_VIDEO_MASK    0x00000400
#define FILE_NOTIFY_CMD_END_VIDEO_MASK    0x00000800
#define FILE_NOTIFY_CMD_DEL_FILE_MASK     0x00001000
#define FILE_NOTIFY_CMD_DEL_DIR_MASK      0x00002000
#define FILE_NOTIFY_CMD_FORMAT_MASK       0x00004000
#define FILE_NOTIFY_RSP_VID_READY_MASK    0x00008000

#define FILE_NOTIFY_GUI_GET_CATALOG_MASK  0x00010000
#define FILE_NOTIFY_GUI_GET_IMAGE_MASK    0x00020000
#define FILE_NOTIFY_GUI_GET_VIDEO_MASK    0x00040000
#define FILE_NOTIFY_GUI_END_VIDEO_MASK    0x00080000
#define FILE_NOTIFY_GUI_PLAY_VIDEO_MASK   0x00100000
#define FILE_NOTIFY_GUI_PAUSE_VIDEO_MASK  0x00200000
#define FILE_NOTIFY_GUI_DEL_FILE_MASK     0x00400000
#define FILE_NOTIFY_GUI_DEL_DIR_MASK      0x00800000
#define FILE_NOTIFY_GUI_FORMAT_MASK       0x01000000


// Maximum file write size - maximum bytes to write through the system call so that
// we don't put too large a pressure on the stack or heap
#define MAX_FILE_WRITE_LEN                8192

// Maximum file read size - maximum bytes to read through the system call.  Limited
// because buffer is allocated from internal heap
#define MAX_FILE_READ_LEN                 4096

// Read length for reading the section, at the end, of a video file that contain
// the video_info record.  Must be <= MAX_FILE_READ_LEN.
#define VIDEO_INFO_READ_LEN               256

// Period between checks for card present state.
#define FILE_CARD_CHECK_PERIOD_MSEC       2000

// Catalog request sources
#define FILE_REQ_SRC_CMD                  0
#define FILE_REQ_SRC_GUI                  1



//
// File Task API
//
void file_task();
bool file_card_present();
void file_set_record_parameters(uint32_t delay_ms, uint32_t num_frames);
void file_set_catalog_index(int src, int type);
char* file_get_catalog(int src, int* num, int* type);
void file_set_get_image(int src, char* dir_name, char* file_name);
void file_set_del_dir(int src, char* dir_name);
void file_set_del_image(int src, char* dir_name, char* file_name);
char* file_get_rsp_file_text(int* len);


#endif /* FILE_TASK_H */