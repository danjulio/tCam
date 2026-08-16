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
#include "app_task.h"
#include "file_task.h"
#include "gcore_task.h"
#include "gui_task.h"
#include "lep_task.h"
#include "rsp_task.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "file_utilities.h"
#include "gui_utilities.h"
#include "power_utilities.h"
#include "ps_utilities.h"
#include "sys_utilities.h"
#include "time_utilities.h"
#include "system_config.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>



//
// App Task private constants
//



//
// App Task variables
//
static const char* TAG = "app_task";

static bool sdcard_present = false;
static bool filesystem_ready = false;  // Can't access images to read or write unless ready
static bool app_recording = false;
static bool img_req_from_gui;
static bool fw_upd_in_progress = false;

// Record rate/duration control
static uint32_t cmd_record_frame_delay_msec;       // mSec between images; 0 = fast as possible
static uint32_t cmd_record_frame_num;              // Number of frames to record; 0 = infinite
static char record_filename[FILE_NAME_LEN];



//
// App Task Forward Declarations for internal functions
//
static void app_task_handle_notifications();
static void app_task_take_picture(bool from_gui);
static void app_task_start_recording(bool from_gui);
static void app_task_stop_recording();


//
// App Task API
//

void app_task()
{
	ESP_LOGI(TAG, "Start task");
	
	// Let other tasks be created and start running first
	while ((task_handle_cmd == NULL) || (task_handle_gcore == NULL) || (task_handle_gui == NULL) ||
	       (task_handle_file == NULL) || (task_handle_lep == NULL) || (task_handle_rsp == NULL)) {
	    
	    vTaskDelay(pdMS_TO_TICKS(100));
	}
	vTaskDelay(pdMS_TO_TICKS(100));
	
	ESP_LOGI(TAG, "Running");
	
	// Initiate transfer of lepton images
	xTaskNotify(task_handle_lep, LEP_NOTIFY_EN_GUI_FRAME_MASK, eSetBits);
	
	while (1) {
		// Look for notifications to act on
		app_task_handle_notifications();

		vTaskDelay(pdMS_TO_TICKS(APP_EVAL_MSEC));
	}
}


void app_set_write_filename(char* name)
{
	char c;
	int i = 0;
	int n;
	
	n = sizeof(record_filename);
	c = *name;
	while ((i < (n-1)) && (c != 0)) {
		record_filename[i++] = c;
		c = *(name + i);
	}
	record_filename[i] = 0; // terminate
}

// Called before sending APP_NOTIFY_CMD_START_RECORD_MASK
void app_set_cmd_record_parameters(uint32_t delay_ms, uint32_t num_frames)
{
	cmd_record_frame_delay_msec = delay_ms;
	cmd_record_frame_num = num_frames;
}


//
// App Task internal functions
//

/**
 * Process notifications from other tasks
 */
static void app_task_handle_notifications()
{
	uint32_t notification_value = 0;
	
	// Handle notifications (clear them upon reading)
	if (xTaskNotifyWait(0x00, 0xFFFFFFFF, &notification_value, 0)) {
		//
		// SHUTDOWN
		//
		if (Notification(notification_value, APP_NOTIFY_SHUTDOWN_MASK)) {
			if (!fw_upd_in_progress) {
				if (app_recording) {
					app_task_stop_recording();
					vTaskDelay(pdMS_TO_TICKS(100));
				}
				
				ESP_LOGI(TAG, "Shutdown");
				xTaskNotify(task_handle_gcore, GCORE_NOTIFY_SHUTDOWN_MASK, eSetBits);
				
				// If we're still alive after a second then assume gcore_task is dead
				// and try to directly cut power
				vTaskDelay(pdMS_TO_TICKS(1000));
				power_off();
			}
		}
		
		
		//
		// PICTURE AND RECORDING CONTROL - CMD or GUI
		//
		if (Notification(notification_value, APP_NOTIFY_GUI_TAKE_PICTURE_MASK)) {
			if (app_recording) {
				app_task_stop_recording();
			}
			app_task_take_picture(true);
		}
		
		if (Notification(notification_value, APP_NOTIFY_CMD_TAKE_PICTURE_MASK)) {
			if (app_recording) {
				app_task_stop_recording();
			}
			app_task_take_picture(false);
		}
		
		if (Notification(notification_value, APP_NOTIFY_GUI_START_RECORD_MASK)) {
			app_task_start_recording(true);
		}
		
		if (Notification(notification_value, APP_NOTIFY_CMD_START_RECORD_MASK)) {
			app_task_start_recording(false);
		}
		
		if (Notification(notification_value, APP_NOTIFY_GUI_STOP_RECORD_MASK)) {
			app_task_stop_recording();
		}
		
		if (Notification(notification_value, APP_NOTIFY_CMD_STOP_RECORD_MASK)) {
			app_task_stop_recording();
		}
		
		if (Notification(notification_value, APP_NOTIFY_GUI_DEL_FILE_MASK)) {
			xTaskNotify(task_handle_file, FILE_NOTIFY_GUI_DEL_FILE_MASK, eSetBits);
		}
		
		if (Notification(notification_value, APP_NOTIFY_CMD_DEL_FILE_MASK)) {
			xTaskNotify(task_handle_file, FILE_NOTIFY_CMD_DEL_FILE_MASK, eSetBits);
		}
		
		if (Notification(notification_value, APP_NOTIFY_GUI_DEL_DIR_MASK)) {
			xTaskNotify(task_handle_file, FILE_NOTIFY_GUI_DEL_DIR_MASK, eSetBits);
		}
		
		if (Notification(notification_value, APP_NOTIFY_CMD_DEL_DIR_MASK)) {
			xTaskNotify(task_handle_file, FILE_NOTIFY_CMD_DEL_DIR_MASK, eSetBits);
		}
		
		if (Notification(notification_value, APP_NOTIFY_GUI_FORMAT_MASK)) {
			xTaskNotify(task_handle_file, FILE_NOTIFY_GUI_FORMAT_MASK, eSetBits);
		}
		
		if (Notification(notification_value, APP_NOTIFY_CMD_FORMAT_MASK)) {
			xTaskNotify(task_handle_file, FILE_NOTIFY_CMD_FORMAT_MASK, eSetBits);
		}
		
		if (Notification(notification_value, APP_NOTIFY_PB_CMD_FAIL_MASK)) {
			// Notify application of failure
			rsp_set_cam_info_msg(RSP_INFO_CMD_NACK, "Error reading image file");
		}
		
		if (Notification(notification_value, APP_NOTIFY_PB_GUI_FAIL_MASK)) {
			gui_preset_message_box_string("Error reading image file", false);
			xTaskNotify(task_handle_gui, GUI_NOTIFY_MESSAGEBOX_MASK, eSetBits);
		}
		
		
		//
		// FILE OPERATIONS
		//
		if (Notification(notification_value, APP_NOTIFY_SDCARD_PRESENT_MASK)) {
			sdcard_present = true;
		}
		
		if (Notification(notification_value, APP_NOTIFY_SDCARD_MISSING_MASK)) {
			filesystem_ready = false;
			sdcard_present = false;
			
			if (app_recording) {
				// Notify ourselves that recording has stopped
				xTaskNotify(task_handle_app, APP_NOTIFY_RECORD_STOP_MASK, eSetBits);
			}
			
			// Let GUI know
			xTaskNotify(task_handle_gui, GUI_NOTIFY_SDCARD_MISSING_MASK, eSetBits);
		}
		
		if (Notification(notification_value, APP_NOTIFY_FILESYSTEM_READY_MASK)) {
			filesystem_ready = true;
		}
		
		if (Notification(notification_value, APP_NOTIFY_RECORD_IMG_DONE_MASK)) {
			// Setup filename to be displayed
			gui_set_write_filename(record_filename);
			
			// Notify GUI a picture was taken
			xTaskNotify(task_handle_gui, GUI_NOTIFY_RECORD_IMG_MASK, eSetBits);
			
			if (!img_req_from_gui) {
				// Notify application of success
				rsp_set_cam_info_msg(RSP_INFO_CMD_ACK, "take_picture success");
			}
		}
		
		if (Notification(notification_value, APP_NOTIFY_RECORD_START_MASK)) {
			app_recording = true;
			
			// Setup filename to be displayed
			gui_set_write_filename(record_filename);
			
			// Notify GUI a recording has been started
			xTaskNotify(task_handle_gui, GUI_NOTIFY_RECORD_ON_MASK, eSetBits);
			
			if (!img_req_from_gui) {
				// Notify application
				rsp_set_cam_info_msg(RSP_INFO_CMD_ACK, "record_on success");
			}
		}
		
		if (Notification(notification_value, APP_NOTIFY_RECORD_STOP_MASK)) {
			app_recording = false;
			xTaskNotify(task_handle_gui, GUI_NOTIFY_RECORD_OFF_MASK, eSetBits);
			
			if (!img_req_from_gui) {
				// Notify application
				rsp_set_cam_info_msg(RSP_INFO_CMD_ACK, "record_off success");
			}
		}
		
		if (Notification(notification_value, APP_NOTIFY_RECORD_FAIL_MASK)) {
			app_recording = false;
			xTaskNotify(task_handle_gui, GUI_NOTIFY_RECORD_FAIL_MASK, eSetBits);
			app_task_stop_recording();
			
			if (img_req_from_gui) {
				// Notify gui of error
				gui_preset_message_box_string("Could not store image to SD Card", false);
				xTaskNotify(task_handle_gui, GUI_NOTIFY_MESSAGEBOX_MASK, eSetBits);
			} else {
				// Notify application of error
				rsp_set_cam_info_msg(RSP_INFO_CMD_NACK, "Could not store image to SD Card");
			}
		}
		
		if (Notification(notification_value, APP_NOTIFY_CMD_DEL_SUCCESS_MASK)) {
			rsp_set_cam_info_msg(RSP_INFO_CMD_ACK, "delete_filesystem_obj success");
		}
		
		if (Notification(notification_value, APP_NOTIFY_CMD_DEL_FAIL_MASK)) {
			rsp_set_cam_info_msg(RSP_INFO_CMD_NACK, "Could not delete image");
		}
		
		if (Notification(notification_value, APP_NOTIFY_GUI_DEL_SUCCESS_MASK)) {
			xTaskNotify(task_handle_gui, GUI_NOTIFY_FILE_DEL_FILE_DONE_MASK, eSetBits);
		}
		
		if (Notification(notification_value, APP_NOTIFY_GUI_DEL_FAIL_MASK)) {
			gui_preset_message_box_string("Could not delete image", false);
			xTaskNotify(task_handle_gui, GUI_NOTIFY_MESSAGEBOX_MASK, eSetBits);
		}
		
		
		//
		// FW UPDATE
		//
		if (Notification(notification_value, APP_NOTIFY_FW_UPD_REQ)) {
			// Notify the user an update has been requested
			gui_preset_message_box_string("Firmware update requested.  Start update?", true);
			xTaskNotify(task_handle_gui, GUI_NOTIFY_FW_UPD_REQ_MB_MASK, eSetBits);
			fw_upd_in_progress = false;
		}
		
		if (Notification(notification_value, APP_NOTIFY_FW_UPD_PROCESS)) {
			// Notify GUI task to display the update progress
			xTaskNotify(task_handle_gui, GUI_NOTIFY_FW_UPD_START_MASK, eSetBits);
			fw_upd_in_progress = true;
		}

		if (Notification(notification_value, APP_NOTIFY_FW_UPD_DONE)) {
			if (fw_upd_in_progress) {
				// Ask the user if they want to reboot now
				gui_preset_message_box_string("Firmware update Complete.  Reboot?", true);
				xTaskNotify(task_handle_gui, GUI_NOTIFY_MESSAGEBOX_MASK, eSetBits);
				fw_upd_in_progress = false;
			} else {
				// Assume the update request has timed out and close the messagebox
				xTaskNotify(task_handle_gui, GUI_NOTIFY_FW_UPD_END_MB_MASK, eSetBits);
			}
		}
		
		if (Notification(notification_value, APP_NOTIFY_FW_UPD_FAIL)) {
			// Switch the main screen
			xTaskNotify(task_handle_gui, GUI_NOTIFY_FW_UPD_STOP_MASK, eSetBits);
			
			// Tell the user the update failed
			gui_preset_message_box_string("Firmware update failed", false);
			xTaskNotify(task_handle_gui, GUI_NOTIFY_MESSAGEBOX_MASK, eSetBits);
			fw_upd_in_progress = false;
		}
		
		
		//
		// WIFI CONFIGURATION
		//
		if (Notification(notification_value, APP_NOTIFY_NEW_WIFI_MASK)) {
			// Reconfigure WiFi
			if (!wifi_reinit()) {
				gui_preset_message_box_string("Could not restart WiFi with the new configuration", false);
				xTaskNotify(task_handle_gui, GUI_NOTIFY_MESSAGEBOX_MASK, eSetBits);
			}				
		}
	}
}


static void app_task_take_picture(bool from_gui)
{
	img_req_from_gui = from_gui;
	
	// Make sure the filesystem is ready
	if (filesystem_ready) {
		// Request file_task start a recording session
		xTaskNotify(task_handle_file, FILE_NOTIFY_STORE_IMAGE_MASK, eSetBits);
	} else {
		if (from_gui) {
			// Let the user know we couldn't start recording
			if (sdcard_present) {
				gui_preset_message_box_string("Indexing.  Please wait a moment", false);
			} else {
				gui_preset_message_box_string("Please insert a SD Card", false);
			}
			xTaskNotify(task_handle_gui, GUI_NOTIFY_MESSAGEBOX_MASK, eSetBits);
		} else {
			// Return a message
			if (sdcard_present) {
				rsp_set_cam_info_msg(RSP_INFO_CMD_NACK, "Cannot take picture.  Indexing SD Card");
			} else {
				rsp_set_cam_info_msg(RSP_INFO_CMD_NACK, "Cannot take picture.  No SD Card installed");
			}
		}
	}
}


static void app_task_start_recording(bool from_gui)
{
	gui_state_t* gui_stP = gui_get_gui_st();
	
	img_req_from_gui = from_gui;
	
	if (!app_recording) {
		// Make sure the filesystem is ready
		if (sdcard_present) {
			// Setup the recording parameters
			if (from_gui) {
				file_set_record_parameters(gui_stP->recording_interval, 0);
			} else {
				file_set_record_parameters(cmd_record_frame_delay_msec, cmd_record_frame_num);
			}
					
			// Request file_task start a recording session
			xTaskNotify(task_handle_file, FILE_NOTIFY_START_RECORDING_MASK, eSetBits);
		} else {
			if (from_gui) {
				// Let the user know we couldn't start recording
				if (sdcard_present) {
					gui_preset_message_box_string("Indexing.  Please wait a moment", false);
				} else {
					gui_preset_message_box_string("Please insert a SD Card", false);
				}
				xTaskNotify(task_handle_gui, GUI_NOTIFY_MESSAGEBOX_MASK, eSetBits);
			} else {
				// Return a message
				if (sdcard_present) {
					rsp_set_cam_info_msg(RSP_INFO_CMD_NACK, "Cannot start recording.  Indexing SD Card");
				} else {
					rsp_set_cam_info_msg(RSP_INFO_CMD_NACK, "Cannot start recording.  No SD Card installed");
				}
			}
		}
	}
}


static void app_task_stop_recording()
{
	if (app_recording) {
		// Request file_task stop a recording session
		xTaskNotify(task_handle_file, FILE_NOTIFY_STOP_RECORDING_MASK, eSetBits);
	}
}
