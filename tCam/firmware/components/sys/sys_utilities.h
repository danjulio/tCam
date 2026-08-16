/*
 * System related utilities
 *
 * Contains functions to initialize the system, other utility functions, a set
 * of globally available handles for the various tasks (to use for task notifications).
 *
 * Copyright 2020-2023 Dan Julio
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
#ifndef SYS_UTILITIES_H
#define SYS_UTILITIES_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lvgl/lvgl.h"
#include "system_config.h"
#include <stdbool.h>
#include <stdint.h>


//
// System Utilities constants
//


//
// System Utilities typedefs
//
typedef struct {
	bool telem_valid;
	uint16_t lep_min_val;
	uint16_t lep_min_x;
	uint16_t lep_min_y;
	uint16_t lep_max_val;
	uint16_t lep_max_x;
	uint16_t lep_max_y;
	uint16_t* lep_bufferP;
	uint16_t* lep_telemP;
	SemaphoreHandle_t mutex;
} lep_buffer_t;

typedef struct {
	uint32_t length;
	char* bufferP;
	SemaphoreHandle_t mutex;
} json_string_t;

typedef struct {
	int length;
	char* pushP;
	char* popP;
	char* bufferP;
	SemaphoreHandle_t mutex;
} json_cmd_response_queue_t;



//
// System Utilities macros
//
#define Notification(var, mask) ((var & mask) == mask)



//
// Task handle externs for use by tasks to communicate with each other
//
extern TaskHandle_t task_handle_app;
extern TaskHandle_t task_handle_cmd;
extern TaskHandle_t task_handle_file;
extern TaskHandle_t task_handle_gcore;
extern TaskHandle_t task_handle_gui;
extern TaskHandle_t task_handle_lep;
extern TaskHandle_t task_handle_rsp;
#ifdef INCLUDE_SYS_MON
extern TaskHandle_t task_handle_mon;
#endif

//
// Global buffer pointers for memory allocated in the external SPIRAM
//

// Shared memory data structures
extern lep_buffer_t lep_gui_buffer[2];    // Loaded by lep_task for gui_task (ping-pong)
extern lep_buffer_t file_gui_buffer[2];   // Loaded by file_task for gui_task (ping-pong)

extern json_string_t lep_spi_buffer;      // Loaded by lep_task SPI read for each image
extern json_string_t lep_rsp_buffer[2];   // Loaded by lep_task for rsp_task (ping-pong)
extern json_string_t lep_file_buffer[2];  // Loaded by lep_task for file_task (ping-pong)

extern json_cmd_response_queue_t lep_cmd_buffer;  // Loaded by other tasks with commands for lep_task to forward

extern json_string_t sys_response_cmd_buffer; // Used by cmd_task for json formatted response data (other than image)
extern json_string_t sys_response_rsp_buffer; // Used by rsp_task for json formatted response data (other than image)

extern json_string_t sys_cmd_lep_buffer;      // Used by lepton_utilities for json formatted commands for lep_task

extern json_cmd_response_queue_t sys_cmd_response_buffer; // Loaded by cmd_task or lep_task with json formatted response data

extern char* rx_circular_buffer;                          // Used by cmd_task for incoming json data
extern char* json_cmd_string;                             // Used by cmd_task to hold a parsed incoming json command

// Firmware update segment
extern uint8_t fw_upd_segment[];                          // Loaded by cmd_utilities for consumption in rsp_task

// ??? rsp_file_text should be migrated to json_string_t for use of mutex ???
// ??? Is a double buffer necessary for gui_file_text  since it's handled inside file_task ???
extern char* gui_file_text[2];                     // Ping-pong buffer used by file_task to store json text for gui_task
extern char* rsp_file_text[2];                     // Ping-pong buffer used by file_task to store json text for rsp_task

// GUI related image buffers
extern uint16_t* gui_render_buffer;                // Used by the gui render functions
extern uint16_t* gui_lep_canvas_buffer;            // Loaded by gui_task for its own use
extern lv_color_t* gui_cmap_canvas_buffer;

extern void* file_info_bufferP;      // Loaded by file_task with the filesystem information structure



//
// System Utilities API
//
bool system_esp_io_init();
bool system_peripheral_init();
bool system_buffer_init();
void system_shutoff();
 
#endif /* SYS_UTILITIES_H */