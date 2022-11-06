/*
 * Lepton Task
 *
 * Contains functions to initialize the tCam-Mini and then receive images and responses
 * from it, making images available to other tasks through a shared ping-pong buffers and
 * the event interface.
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
#ifndef LEP_TASK_H
#define LEP_TASK_H

#include <stdint.h>



//
// LEP Task Constants
//

// Evaluation interval
#define LEP_TASK_EVAL_MSEC 10

// tCam-Mini re-initialization timeout - attempt to re-initialize the camera
// if communication hasn't been received within this timeout
#define LEP_TASK_INIT_TO_MSEC    5000

// Mutex wait time - time to try to acquire a mutex for another tasks's ping-pong
// buffer before giving up so we can service everyone else (they'll loose an image)
#define LEP_TASK_MUTEX_WAIT_MSEC 10

// LEP Task notifications
#define LEP_NOTIFY_EN_RSP_FRAME_MASK   0x00000001
#define LEP_NOTIFY_DIS_RSP_FRAME_MASK  0x00000002
#define LEP_NOTIFY_EN_FILE_FRAME_MASK  0x00000010
#define LEP_NOTIFY_DIS_FILE_FRAME_MASK 0x00000020
#define LEP_NOTIFY_EN_GUI_FRAME_MASK   0x00000100
#define LEP_NOTIFY_DIS_GUI_FRAME_MASK  0x00000200



//
// LEP Task API
//
void lep_task();
bool lep_available();
char* lep_get_version();

#endif /* LEP_TASK_H */