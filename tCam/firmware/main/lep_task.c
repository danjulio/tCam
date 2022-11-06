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
#include <stdbool.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "app_task.h"
#include "cmd_task.h"
#include "file_task.h"
#include "gui_task.h"
#include "lep_task.h"
#include "rsp_task.h"
#include "json_utilities.h"
#include "lepton_utilities.h"
#include "sif_utilities.h"
#include "sys_utilities.h"
#include "system_config.h"


//
// LEP Task constants
//

// Uncomment to debug
//#define LOG_SEND_TIMESTAMP
//#define DEBUG_RSP
//#define LOG_SPI_PASS_FAIL

// Local json string buffer size
#define JSON_BUF_LEN (2 * JSON_MAX_RSP_TEXT_LEN)



//
// LEP Task variables
//
static const char* TAG = "lep_task";

// State
static bool tcam_mini_detected;

// SPI Interface
static spi_device_handle_t spi;
static spi_transaction_t lep_spi_trans;

// Image request flags - set and cleared by notification
static bool cmd_image_requested;
static bool gui_image_requested;
static bool file_image_requested;

// GUI lep_gui_buffer index
static int gui_image_index;

// RSP and FILE buffer index
static int json_image_index;

// json string buffers
static char json_rsp_buffer[JSON_BUF_LEN];    // Buffer for processing incoming serial data
static char json_rsp[JSON_MAX_RSP_TEXT_LEN];  // Single json response
static int rsp_push_index;
static int rsp_pop_index;



//
// LEP Task forward declarations for internal functions
//
static bool init_spi();
static void process_cmd_data();
static bool rx_rsp_available();
static void process_rx_response();
static void process_status(cJSON* json_obj);
static void process_image(cJSON* json_obj);
static bool check_checksum(uint32_t exp_cs);
static void push_response(char* buf, uint32_t len);



//
// LEP Task API
//

/**
 * This task drives the Lepton camera interface.
 */
void lep_task()
{
	int64_t last_comm_time;
	uint32_t notification_value;
	
	ESP_LOGI(TAG, "Start task");
	
	// Initialize global vars
	tcam_mini_detected = false;
	cmd_image_requested = false;
	gui_image_requested = false;
	file_image_requested = false;
	gui_image_index = 0;
	json_image_index = 0;
	rsp_push_index = 0;
	rsp_pop_index = 0;
	
	// Initialize the serial interface
	sif_init();
	
	// Initialize the SPI interface
	if (!init_spi()) {
		vTaskDelete(NULL);
	}
	
	// Initialize tCam-Mini
	lepton_init();
	last_comm_time = esp_timer_get_time();
	
	while (true) {
		// See what tasks want images (clear them upon reading)
		if (xTaskNotifyWait(0x00, 0xFFFFFFFF, &notification_value, 0)) {;
			if (Notification(notification_value, LEP_NOTIFY_EN_RSP_FRAME_MASK)) {
				cmd_image_requested = true;
			}
			if (Notification(notification_value, LEP_NOTIFY_DIS_RSP_FRAME_MASK)) {
				cmd_image_requested = false;
			}
			if (Notification(notification_value, LEP_NOTIFY_EN_FILE_FRAME_MASK)) {
				file_image_requested = true;
			}
			if (Notification(notification_value, LEP_NOTIFY_DIS_FILE_FRAME_MASK)) {
				file_image_requested = false;
			}
			if (Notification(notification_value, LEP_NOTIFY_EN_GUI_FRAME_MASK)) {
				gui_image_requested = true;
			}
			if (Notification(notification_value, LEP_NOTIFY_DIS_GUI_FRAME_MASK)) {
				gui_image_requested = false;
			}
		}
		
		// Send commands to the tCam-Mini
		process_cmd_data();
		
		// Look for tCam-Mini responses to process
		if (rx_rsp_available()) {
			process_rx_response();
			last_comm_time = esp_timer_get_time();
			tcam_mini_detected = true;
		}
		
		// Attempt to re-initialize tCam-Mini if we haven't seen any communication from
		// it in a period (perhaps because it was reset or re-powered and is no longer
		// streaming to us)
		if ((esp_timer_get_time() - last_comm_time) > (LEP_TASK_INIT_TO_MSEC * 1000)) {
			ESP_LOGI(TAG, "Re-initialize tCam-Mini");
			rsp_push_index = 0;
			rsp_pop_index = 0;
			lepton_init();
			last_comm_time = esp_timer_get_time();
			tcam_mini_detected = false;
		}
		
		vTaskDelay(pdMS_TO_TICKS(LEP_TASK_EVAL_MSEC));
	}
}


bool lep_available()
{
	// yeah, yeah, yeah, I should theoretically protect this with a semaphore.  But it's a bool
	// so we'll assume access to that is atomic enough.
	return tcam_mini_detected;
}



//
// LEP Task Internal functions
//
static bool init_spi()
{
	esp_err_t ret;
  
	spi_device_interface_config_t devcfg = {
		.command_bits = 0,
		.address_bits = 0,
		.clock_speed_hz = LEP_SPI_FREQ_HZ,
		.mode = LEP_SPI_MODE,
		.spics_io_num = LEP_CSN_IO,
		.queue_size = 1,
		.flags = SPI_DEVICE_HALFDUPLEX,
		.cs_ena_pretrans = 10
	};

	if ((ret=spi_bus_add_device(LEP_SPI_HOST, &devcfg, &spi)) != ESP_OK) {
		ESP_LOGE(TAG, "failed to add lepton spi device");
	}
	
	// Setup the invariant parts of the SPI transaction
	memset(&lep_spi_trans, 0, sizeof(spi_transaction_t));
	lep_spi_trans.tx_buffer = NULL;
	lep_spi_trans.rx_buffer = lep_spi_buffer.bufferP;

	return (ret == ESP_OK);
}


static void process_cmd_data()
{
	char buf[128];
	char c;
	int i = 0;
	int len;
	int remain;
	
	// Process all available data in the command buffer
	len = lepton_cmd_data_available();
	remain = len;
	while (remain-- != 0) {
		c = lepton_pop_cmd_buffer();
		buf[i++] = c;
		
		if (i == 128) {
			sif_send(buf, i);
			i = 0;
		}
	}
	lepton_dec_cmd_buffer_len(len);
	
	// Send any final portion
	if (i != 0) {
		sif_send(buf, i);
	}	
}


static bool rx_rsp_available()
{
	bool saw_rsp = false;
	char buf[128];
	char c;
	int i = 0;
	int len;
	
	// Copy all data from the serial interface into json_rsp_buffer
	len = sif_get(buf, 128);
	while (len != 0) {
		c = buf[i++];
		*(json_rsp_buffer + rsp_push_index) = c;
		if (++rsp_push_index == JSON_BUF_LEN) rsp_push_index = 0;
		
		// Check for more serial data
		if (--len == 0) {
			len = sif_get(buf, 128);
			i = 0;
		}
	}
	
	// Look for complete json string
	i = rsp_pop_index;
	while (i != rsp_push_index) {
		c = *(json_rsp_buffer + i);
		if (++i == JSON_BUF_LEN) i = 0;
		
		if (c == CMD_JSON_STRING_STOP) {
			saw_rsp = true;
			break;
		}
	}
	
	return saw_rsp;
}


static void process_rx_response()
{
	char c = 0;
	int i = 0;
	cJSON* json_obj;
	
	// Get the json response string
	//  Index 0 has the start delimiter character
	//  The last index has the end delimiter character
	while (c != CMD_JSON_STRING_STOP) {
		c = *(json_rsp_buffer + rsp_pop_index);
		if (++rsp_pop_index == JSON_BUF_LEN) rsp_pop_index = 0;
		json_rsp[i++] = c;
	}
	json_rsp[i] = 0;

	// Create a json object to parse starting with the first character in the json string
	json_obj = json_get_object(&json_rsp[1]);
#ifdef DEBUG_RSP
	ESP_LOGI(TAG, "RX %s", json_rsp); 
#endif
	if (json_obj != NULL) {
		// Parse and process responses we are interested in (ignore the rest)	
		if (cJSON_HasObjectItem(json_obj, "image_ready")) {
			process_image(json_obj);
		}
		else if (cJSON_HasObjectItem(json_obj, "cci_reg")) {
			// Just push response string to rsp_task since this is the result
			// of a get_cci_reg or set_cci_reg command
			push_response(json_rsp, i);
		}
		else if (cJSON_HasObjectItem(json_obj, "cam_info")) {
			// Push cam_info response string if it is from a cci_reg command since
			// we want to report any failures if they occur (the command will have 
			// returned the cci_reg response above if successful)
			if (strstr(json_rsp, "cci") != NULL) {
				push_response(json_rsp, i);
			}
		}
		else if (cJSON_HasObjectItem(json_obj, "status")) {
			// Should only see this when tCam-Mini is initialized
			process_status(json_obj);
			
			// Start streaming
			lepton_stream_on();
		}
		
		json_free_object(json_obj);
	} else {
		ESP_LOGE(TAG, "could not process json_rsp: %s", json_rsp);
	}
}


static void process_status(cJSON* json_obj)
{
	cJSON* args;
	
	args = cJSON_GetObjectItem(json_obj, "status");
	
	// Update the global tcam_mini_status for others to consume
	if (args != NULL) {
		json_parse_status_rsp(args, &tcam_mini_status);
		
		ESP_LOGI(TAG, "tCam-Mini config:");
		ESP_LOGI(TAG, "  FW v%s", tcam_mini_status.version);
		switch (lepton_get_model()) {
			case LEP_TYPE_3_0:
				ESP_LOGI(TAG, "  Lepton 3.0");
				break;
			case LEP_TYPE_3_1:
				ESP_LOGI(TAG, "  Lepton 3.1");
				break;
			case LEP_TYPE_3_5:
				ESP_LOGI(TAG, "  Lepton 3.5");
				break;
			default:
				ESP_LOGI(TAG, "  Unknown Lepton");
		}
	}
}


static void process_image(cJSON* json_obj)
{
	bool good_checksum;
	uint32_t exp_cs;
	uint32_t mask;

#ifdef LOG_SEND_TIMESTAMP
	int64_t tb, te;
#endif
	
	// Get the image length from the response and then setup and execute the SPI read
	if (json_parse_image_ready(json_obj, &(lep_spi_buffer.length))) {
		if (lep_spi_buffer.length > JSON_MAX_IMAGE_TEXT_LEN) {
			ESP_LOGE(TAG, "image_ready length %d exceeds maximum", lep_spi_buffer.length);
			return;
		}
		
		// Length (for DMA) must be multiple of 4 bytes
		if (lep_spi_buffer.length & 0x3) {
			// Round up to next 4-byte boundary
			lep_spi_trans.rxlength = ((lep_spi_buffer.length + 4) & 0xFFFFFFFC) * 8;
		} else {
			lep_spi_trans.rxlength = lep_spi_buffer.length * 8;
		}

#ifdef LOG_SEND_TIMESTAMP
		tb = esp_timer_get_time();
#endif
		if (spi_device_transmit(spi, &lep_spi_trans) == ESP_OK) {
			// Get the expected checksum from the data just read in (last four bytes)
			exp_cs  = (uint32_t) (*(lep_spi_buffer.bufferP + lep_spi_buffer.length - 4) << 24);
			exp_cs |= (uint32_t) (*(lep_spi_buffer.bufferP + lep_spi_buffer.length - 3) << 16);
			exp_cs |= (uint32_t) (*(lep_spi_buffer.bufferP + lep_spi_buffer.length - 2) << 8);
			exp_cs |= (uint32_t) (*(lep_spi_buffer.bufferP + lep_spi_buffer.length - 1));
#ifdef LOG_SEND_TIMESTAMP
				te = esp_timer_get_time();
				ESP_LOGI(TAG, "spi_device_transmit took %d uSec", (int) (te - tb));
				
				tb = esp_timer_get_time();
#endif

			// Checksum
			good_checksum = check_checksum(exp_cs);
#ifdef DEBUG_RSP
			if (!good_checksum) {
				ESP_LOGE(TAG, "bad checksum");
			}
#endif
			
			// Copy the json string to rsp_task and/or file_task if requested
			 if (cmd_image_requested && good_checksum) {
			 	if (xSemaphoreTake(lep_rsp_buffer[json_image_index].mutex, pdMS_TO_TICKS(LEP_TASK_MUTEX_WAIT_MSEC))) {
			 		memcpy(lep_rsp_buffer[json_image_index].bufferP, lep_spi_buffer.bufferP, lep_spi_buffer.length - 4);
			 		lep_rsp_buffer[json_image_index].length = lep_spi_buffer.length - 4;
			 		xSemaphoreGive(lep_rsp_buffer[json_image_index].mutex);
			 		mask = (json_image_index == 0) ? RSP_NOTIFY_LEP_FRAME_MASK_1 : RSP_NOTIFY_LEP_FRAME_MASK_2;
			 		xTaskNotify(task_handle_rsp, mask, eSetBits);
			 	}
			} 
			if (file_image_requested && good_checksum) {
				if (xSemaphoreTake(lep_file_buffer[json_image_index].mutex, pdMS_TO_TICKS(LEP_TASK_MUTEX_WAIT_MSEC))) {
			 		memcpy(lep_file_buffer[json_image_index].bufferP, lep_spi_buffer.bufferP, lep_spi_buffer.length - 4);
			 		lep_file_buffer[json_image_index].length = lep_spi_buffer.length - 4;
			 		xSemaphoreGive(lep_file_buffer[json_image_index].mutex);
			 		mask = (json_image_index == 0) ? FILE_NOTIFY_LEP_FRAME_MASK_1 : FILE_NOTIFY_LEP_FRAME_MASK_2;
			 		xTaskNotify(task_handle_file, mask, eSetBits);
			 	}
			}
			if ((cmd_image_requested || file_image_requested) && good_checksum) {
				// Flip ping-pong index
				json_image_index = (json_image_index == 0) ? 1 : 0;
			}
#ifdef LOG_SEND_TIMESTAMP
				te = esp_timer_get_time();
				ESP_LOGI(TAG, "process took %d uSec", (int) (te - tb));
#endif
			
			// Convert the json image string into a lepton image buffer for gui_task if requested
			if (gui_image_requested && good_checksum) {
#ifdef LOG_SEND_TIMESTAMP
				tb = esp_timer_get_time();
#endif
				mask = 0;
				if (gui_image_index == 0) {
					if (xSemaphoreTake(lep_gui_buffer[0].mutex, pdMS_TO_TICKS(LEP_TASK_MUTEX_WAIT_MSEC))) {
						if (json_parse_image_string(lep_spi_buffer.bufferP, &lep_gui_buffer[0])) {
							mask = GUI_NOTIFY_LEP_FRAME_MASK_1;
						}
						xSemaphoreGive(lep_gui_buffer[0].mutex);
					}
					gui_image_index = 1;
				} else {
					if (xSemaphoreTake(lep_gui_buffer[1].mutex, pdMS_TO_TICKS(LEP_TASK_MUTEX_WAIT_MSEC))) {
						if (json_parse_image_string(lep_spi_buffer.bufferP, &lep_gui_buffer[1])) {
							mask = GUI_NOTIFY_LEP_FRAME_MASK_2;
						}
						xSemaphoreGive(lep_gui_buffer[1].mutex);
					}
					gui_image_index = 0;
				}
				if (mask != 0) {
					xTaskNotify(task_handle_gui, mask, eSetBits);
				}
				
#ifdef LOG_SEND_TIMESTAMP
				te = esp_timer_get_time();
				ESP_LOGI(TAG, "json_parse_image_string took %d uSec", (int) (te - tb));
#endif
			}
			
#ifdef LOG_SPI_PASS_FAIL			
			if (good_checksum) {
				ESP_LOGI(TAG, "good");
			} else {
				ESP_LOGI(TAG, "bad");
			}
#endif
		} else {
			ESP_LOGE(TAG, "SPI transaction failed");
		}
	} else {
		ESP_LOGE(TAG, "Could not parse image_ready");
	}
}


static bool check_checksum(uint32_t exp_cs)
{
	char* ps;
	int len;
	uint32_t act_cs = 0;
	
	// Spin through data computing the checksum
	ps = lep_spi_buffer.bufferP;
	len = lep_spi_buffer.length - 4;  // Don't include the checksum data at the end
	while (len--) {
		act_cs += *ps++;
	}
	
	// Return true only if the checksums match
	return (exp_cs == act_cs);
}


static void push_response(char* buf, uint32_t len)
{
	int i;
	
	// Atomically load cmd_task_response_buffer
	xSemaphoreTake(sys_cmd_response_buffer.mutex, portMAX_DELAY);
	
	// Only load if there's room for this response
	if (len <= (CMD_RESPONSE_BUFFER_LEN - sys_cmd_response_buffer.length)) {
		for (i=0; i<len; i++) {
			// Push data
			*sys_cmd_response_buffer.pushP = *(buf+i);
			
			// Increment push pointer
			if (++sys_cmd_response_buffer.pushP >= (sys_cmd_response_buffer.bufferP + CMD_RESPONSE_BUFFER_LEN)) {
				sys_cmd_response_buffer.pushP = sys_cmd_response_buffer.bufferP;
			}
		}
		
		sys_cmd_response_buffer.length += len;
	}
	
	xSemaphoreGive(sys_cmd_response_buffer.mutex);
}
