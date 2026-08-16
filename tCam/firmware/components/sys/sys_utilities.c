/*
 * System related utilities
 *
 * Contains functions to initialize the system, other utility functions and a set
 * of globally available handles for the various tasks (to use for task notifications).
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
#include "esp_system.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "file_utilities.h"
#include "json_utilities.h"
#include "lepton_utilities.h"
#include "power_utilities.h"
#include "ps_utilities.h"
#include "sys_utilities.h"
#include "time_utilities.h"
#include "gui_screen_main.h"
#include "i2c.h"



//
// System Utilities variables
//
static const char* TAG = "sys";


//
// Task handle externs for use by tasks to communicate with each other
//
TaskHandle_t task_handle_app;
TaskHandle_t task_handle_cmd;
TaskHandle_t task_handle_file;
TaskHandle_t task_handle_gcore;
TaskHandle_t task_handle_gui;
TaskHandle_t task_handle_lep;
TaskHandle_t task_handle_rsp;
#ifdef INCLUDE_SYS_MON
TaskHandle_t task_handle_mon;
#endif


//
// Global buffer pointers for memory allocated in the external SPIRAM
//

// Shared memory data structures
lep_buffer_t lep_gui_buffer[2];    // Loaded by lep_task for gui_task (ping-pong)
lep_buffer_t file_gui_buffer[2];   // Loaded by file_task for gui_task (ping-pong)

json_string_t lep_spi_buffer;      // Loaded by lep_task SPI read for each image
json_string_t lep_rsp_buffer[2];   // Loaded by lep_task for rsp_task (ping-pong)
json_string_t lep_file_buffer[2];  // Loaded by lep_task for file_task (ping-pong)

json_cmd_response_queue_t lep_cmd_buffer;  // Loaded by other tasks with commands for lep_task to forward

json_string_t sys_response_cmd_buffer; // Used by cmd_task for json formatted response data (other than image)
json_string_t sys_response_rsp_buffer; // Used by rsp_task for json formatted response data (other than image)

json_string_t sys_cmd_lep_buffer;      // Used by lepton_utilities for json formatted commands for lep_task

json_cmd_response_queue_t sys_cmd_response_buffer; // Loaded by cmd_task or lep_task with json formatted response data

char* rx_circular_buffer;                          // Used by cmd_task for incoming json data
char* json_cmd_string;                             // Used by cmd_task to hold a parsed incoming json command

// Firmware update segment (located in internal DRAM)
uint8_t fw_upd_segment[FW_UPD_CHUNK_MAX_LEN];      // Loaded by cmd_utilities for consumption in rsp_task

char* gui_file_text[2];                     // Ping-pong buffer used by file_task to store json text for gui_task
char* rsp_file_text[2];                     // Ping-pong buffer used by file_task to store json text for rsp_task

// GUI related image buffers
uint16_t* gui_render_buffer;                // Used by the gui render functions
uint16_t* gui_lep_canvas_buffer;            // Loaded by gui_task for its own use
lv_color_t* gui_cmap_canvas_buffer;

void* file_info_bufferP;      // Loaded by file_task with the filesystem information structure



//
// System Utilities API
//

/**
 * Initialize the ESP32 GPIO and internal peripherals
 */
bool system_esp_io_init()
{
	ESP_LOGI(TAG, "ESP32 Peripheral Initialization");	
	
	// Attempt to initialize the I2C Master
	if (i2c_master_init() != ESP_OK) {
		ESP_LOGE(TAG, "I2C Master initialization failed");
		return false;
	}
	
	// Attempt to initialize the SPI Master used by lep_task to read tCam-Mini
	spi_bus_config_t spi_buscfg1 = {
		.miso_io_num=LEP_MISO_IO,
		.mosi_io_num=-1,
		.sclk_io_num=LEP_SCK_IO,
		.max_transfer_sz=JSON_MAX_IMAGE_TEXT_LEN,
		.quadwp_io_num=-1,
		.quadhd_io_num=-1
	};
	if (spi_bus_initialize(LEP_SPI_HOST, &spi_buscfg1, LEP_DMA_NUM) != ESP_OK) {
		ESP_LOGE(TAG, "Lepton Master initialization failed");
		return false;
	}
	
	return true;
}


/**
 * Initialize the board-level peripheral subsystems
 */
bool system_peripheral_init()
{
	ESP_LOGI(TAG, "System Peripheral Initialization");
	
	if (!power_init()) {
		ESP_LOGE(TAG, "gCore subsystem initialization failed");
		return false;
	}
	
	time_init();
	
	if (!ps_init()) {
		ESP_LOGE(TAG, "Persistent Storage initialization failed");
		return false;
	}
	
	if (!file_init_sdmmc_driver()) {
		ESP_LOGE(TAG, "SD Card driver initialization failed");
		return false;
	}
	
	if (!wifi_init()) {
		ESP_LOGE(TAG, "WiFi initialization failed");
		return false;
	}
	
	return true;
}


/**
 * Allocate shared buffers for use by tasks for image data
 */
bool system_buffer_init()
{
	int i;
	
	ESP_LOGI(TAG, "Buffer Allocation");
	
	// Allocate the gui_task lepton frame and telemetry buffers in the external RAM
	for (i=0; i<2; i++) {
		lep_gui_buffer[i].lep_bufferP = heap_caps_malloc(LEP_NUM_PIXELS*2, MALLOC_CAP_SPIRAM);
		if (lep_gui_buffer[i].lep_bufferP == NULL) {
			ESP_LOGE(TAG, "malloc GUI lepton shared image buffer %d failed", i);
			return false;
		}
		lep_gui_buffer[i].lep_telemP = heap_caps_malloc(LEP_TEL_WORDS*2, MALLOC_CAP_SPIRAM);
		if (lep_gui_buffer[i].lep_telemP == NULL) {
			ESP_LOGE(TAG, "malloc GUI lepton shared telemetry buffer %d failed", i);
			return false;
		}
		lep_gui_buffer[i].mutex = xSemaphoreCreateMutex();
	}
	
	// Allocate the file_task read data frame and telemetry buffers in the external RAM
	for (i=0; i<2; i++) {
		file_gui_buffer[i].lep_bufferP = heap_caps_malloc(LEP_NUM_PIXELS*2, MALLOC_CAP_SPIRAM);
		if (file_gui_buffer[i].lep_bufferP == NULL) {
			ESP_LOGE(TAG, "malloc FILE read shared image buffer %d failed", i);
			return false;
		}
		file_gui_buffer[i].lep_telemP = heap_caps_malloc(LEP_TEL_WORDS*2, MALLOC_CAP_SPIRAM);
		if (file_gui_buffer[i].lep_telemP == NULL) {
			ESP_LOGE(TAG, "malloc FILE read shared telemetry buffer %d failed", i);
			return false;
		}
		file_gui_buffer[i].mutex = xSemaphoreCreateMutex();
	}
	
	// Allocate the lep_task lepton image json string buffer in internal DMA capable RAM
	lep_spi_buffer.mutex = xSemaphoreCreateMutex();
	lep_spi_buffer.bufferP = heap_caps_malloc(JSON_MAX_IMAGE_TEXT_LEN, MALLOC_CAP_DMA);
	if (lep_spi_buffer.bufferP == NULL) {
		ESP_LOGE(TAG, "malloc SPI lepton json buffer failed");
		return false;
	}
				
	// Allocate the cmd/rsp_task lepton image json string buffers in the external RAM
	for (i=0; i<2; i++) {
		lep_rsp_buffer[i].mutex = xSemaphoreCreateMutex();
		lep_rsp_buffer[i].bufferP = heap_caps_malloc(JSON_MAX_IMAGE_TEXT_LEN, MALLOC_CAP_SPIRAM);
		if (lep_rsp_buffer[i].bufferP == NULL) {
			ESP_LOGE(TAG, "malloc RSP lepton shared json buffer %d failed", i);
			return false;
		}
	}
	
	// Allocate the file_task lepton image json string buffers in the external RAM
	for (i=0; i<2; i++) {
		lep_file_buffer[i].mutex = xSemaphoreCreateMutex();
		lep_file_buffer[i].bufferP = heap_caps_malloc(JSON_MAX_IMAGE_TEXT_LEN, MALLOC_CAP_SPIRAM);
		if (lep_file_buffer[i].bufferP == NULL) {		
			ESP_LOGE(TAG, "malloc FILE lepton shared json buffer %d failed", i);
			return false;
		}
	}
	
	// Allocate the outgoing command response json buffer
	lep_cmd_buffer.mutex = xSemaphoreCreateMutex();
	lep_cmd_buffer.bufferP = heap_caps_malloc(LEP_COMMAND_BUFFER_LEN, MALLOC_CAP_SPIRAM);
	if (lep_cmd_buffer.bufferP == NULL) {
		ESP_LOGE(TAG, "malloc CMD response buffer failed");
		return false;
	}
	lep_cmd_buffer.pushP = lep_cmd_buffer.bufferP;
	lep_cmd_buffer.popP = lep_cmd_buffer.bufferP;
	lep_cmd_buffer.length = 0;
	
	// Allocate the buffer used by the gui to render images
	gui_render_buffer = heap_caps_malloc(LEP_IMG_PIXELS*2, MALLOC_CAP_SPIRAM);
	if (gui_render_buffer == NULL) {
		ESP_LOGE(TAG, "malloc GUI render buffer failed");
		return false;
	}
	
	// Allocate the buffer used by the gui to display images from the lepton
	gui_lep_canvas_buffer = heap_caps_malloc(LEP_IMG_PIXELS*2, MALLOC_CAP_SPIRAM);
	if (gui_lep_canvas_buffer == NULL) {
		ESP_LOGE(TAG, "malloc GUI canvas buffer failed");
		return false;
	}
	
	// Allocate the drawing canvas for the current colormap
	gui_cmap_canvas_buffer = (lv_color_t*) heap_caps_malloc(MAIN_CMAP_CANVAS_PIXELS*2, MALLOC_CAP_SPIRAM);
	if (gui_cmap_canvas_buffer == NULL) {
		ESP_LOGE(TAG, "malloc gui_cmap_canvas_buffer failed");
		return false;
	}
	
	// Allocate the json buffers
	if (!json_init()) {
		ESP_LOGE(TAG, "malloc json buffers failed");
		return false;
	}
	
	// Allocate the incoming command buffers
	rx_circular_buffer = heap_caps_malloc(JSON_MAX_CMD_TEXT_LEN, MALLOC_CAP_SPIRAM);
	if (rx_circular_buffer == NULL) {
		ESP_LOGE(TAG, "malloc rx_circular_buffer failed");
		return false;
	}
	json_cmd_string = heap_caps_malloc(JSON_MAX_CMD_TEXT_LEN, MALLOC_CAP_SPIRAM);
	if (json_cmd_string == NULL) {
		ESP_LOGE(TAG, "malloc json_cmd_string failed");
		return false;
	}
	
	// Allocate the outgoing command response json buffer
	sys_cmd_response_buffer.mutex = xSemaphoreCreateMutex();
	sys_cmd_response_buffer.bufferP = heap_caps_malloc(CMD_RESPONSE_BUFFER_LEN, MALLOC_CAP_SPIRAM);
	if (sys_cmd_response_buffer.bufferP == NULL) {
		ESP_LOGE(TAG, "malloc CMD response buffer failed");
		return false;
	}
	sys_cmd_response_buffer.pushP = sys_cmd_response_buffer.bufferP;
	sys_cmd_response_buffer.popP = sys_cmd_response_buffer.bufferP;
	sys_cmd_response_buffer.length = 0;

	// Allocate the json response text buffers in the external RAM
	sys_response_cmd_buffer.bufferP = heap_caps_malloc(JSON_MAX_RSP_TEXT_LEN, MALLOC_CAP_SPIRAM);
	if (sys_response_cmd_buffer.bufferP == NULL) {
		ESP_LOGE(TAG, "Could not allocate sys_response_cmd_buffer buffer");
		return false;
	}
	
	sys_response_rsp_buffer.bufferP = heap_caps_malloc(JSON_MAX_RSP_TEXT_LEN, MALLOC_CAP_SPIRAM);
	if (sys_response_rsp_buffer.bufferP == NULL) {
		ESP_LOGE(TAG, "Could not allocate sys_response_rsp_buffer buffer");
		return false;
	}
	
	// Allocate the lepton command text buffer in the external RAM
	sys_cmd_lep_buffer.bufferP = heap_caps_malloc(JSON_MAX_CMD_TEXT_LEN, MALLOC_CAP_SPIRAM);
	if (sys_cmd_lep_buffer.bufferP == NULL) {
		ESP_LOGE(TAG, "Could not allocate sys_cmd_lep_buffer buffer");
		return false;
	}
	
	// Allocate the file system read ping-pong json string buffers in the external RAM
	gui_file_text[0] = heap_caps_malloc(JSON_MAX_IMAGE_TEXT_LEN, MALLOC_CAP_SPIRAM);
	gui_file_text[1] = heap_caps_malloc(JSON_MAX_IMAGE_TEXT_LEN, MALLOC_CAP_SPIRAM);
	if ((gui_file_text[0] == NULL) || (gui_file_text[1] == NULL)) {
		ESP_LOGE(TAG, "malloc file read GUI ping-pong buffers failed");
		return false;
	}
	
	rsp_file_text[0] = heap_caps_malloc(JSON_MAX_IMAGE_TEXT_LEN, MALLOC_CAP_SPIRAM);
	rsp_file_text[1] = heap_caps_malloc(JSON_MAX_IMAGE_TEXT_LEN, MALLOC_CAP_SPIRAM);
	if ((rsp_file_text[0] == NULL) || (rsp_file_text[1] == NULL)) {
		ESP_LOGE(TAG, "malloc file read RSP ping-pong buffers failed");
		return false;
	}
	
	// Allocate the filesystem information buffer in external RAM
	file_info_bufferP = heap_caps_malloc(FILE_INFO_BUFFER_LEN, MALLOC_CAP_SPIRAM);
	if (file_info_bufferP == NULL) {
		ESP_LOGE(TAG, "malloc filesystem information buffer failed");
		return false;
	}
	
	return true;
}


/**
 * Shut the system off
 */
void system_shutoff()
{
	// Save NVRAM to backing flash in gCore if necessary
	ps_save_to_flash();
	
	ESP_LOGI(TAG, "Power off");
	
	// Delay for final logging
	vTaskDelay(pdMS_TO_TICKS(50));
	
	// Commit Seppuku
	power_off();
}

