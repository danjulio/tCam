/*
 * File Task
 *
 * Handle the SD Card and manage reading and writing files for other tasks.  Create and
 * manage a file system information structure (catalog) of legal tcam files on the currently
 * installed card.  Detect SD Card insertion and removal.
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
#include "file_task.h"
#include "app_task.h"
#include "cmd_task.h"
#include "gui_task.h"
#include "lep_task.h"
#include "rsp_task.h"
#include "file_utilities.h"
#include "json_utilities.h"
#include "power_utilities.h"
#include "time_utilities.h"
#include "sys_utilities.h"
#include "esp_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "cJSON.h"
#include <string.h>


//
// File Task private constants
//
#define FILE_TASK_EVAL_NORM_MSEC 50
#define FILE_TASK_EVAL_FAST_MSEC 10

// Uncomment to log various file processing timestamps
//#define LOG_WRITE_TIMESTAMP
//#define LOG_READ_TIMESTAMP
//#define LOG_VIDEO_TIMING



//
// File Task private variables
//
static const char* TAG = "file_task";

// Counter used to probe card for presence 
static int card_check_count = FILE_CARD_CHECK_PERIOD_MSEC / FILE_TASK_EVAL_NORM_MSEC;

// Hardware card detection state from the hardware switch on the card socket
static bool card_present;

// Recording state
static bool got_lep_image_0;
static bool got_lep_image_1;
static bool recording;
static bool rec_file_open;
static bool rec_image_ready;
static FILE* rec_fp;
static uint32_t num_record_frames;
static tmElements_t rec_start_time;
static tmElements_t rec_stop_time;

// Record rate/duration control
static uint32_t next_record_frame_delay_msec;       // mSec between images; 0 = fast as possible
static uint32_t cur_record_frame_delay_usec;
static uint32_t next_record_frame_num;              // Number of frames to record; 0 = infinite
static uint32_t cur_record_frame_num;
static uint32_t record_remaining_frames;            // Remaining frames to record
static int64_t record_req_usec;                     // ESP32 uSec timestamp of requested record image

// Read state
//  - Used to coordinate reading files and transferring data to gui/rsp tasks
//  - Two sets of state: CMD/RSP and GUI
//  - Associated buffers, gui_file_text and rsp_fil_text store complete json image strings
//    including START/END delimiters
static bool read_file_open[2];
static char read_dir_names[2][FILE_NAME_LEN];
static char read_file_names[2][FILE_NAME_LEN];
static char read_buffers[2][MAX_FILE_READ_LEN+1];   // Statically allocated read buffer
static FILE* read_fp[2];
static int cur_pp_load_index[2];                    // Current ping-pong buffer to load data into
static int cur_pp_read_index[2];                    // Current ping-pong buffer to read data out of
static int cur_pp_length[2][2];                     // Current ping-pong buffer length
static int num_pp_valid[2];                         // Number of ping-pong buffers with data

// Delete state
//  - Used to coordinate deleting files by to gui/cmd tasks
//  - Two sets of state: CMD/RSP and GUI
static char del_dir_names[2][FILE_NAME_LEN];
static char del_file_names[2][FILE_NAME_LEN];

// cJSON object that holds decoded data from a read file (single image or video_info)
// used when supplying images to the GUI for playback
static cJSON* read_json_obj;

// CMD/RSP Video playback control
static bool rsp_ready_for_video_image;               // Set by rsp_task when it is ready for the next image

// GUI Video playback control
static bool video_playing;
static bool video_fixed_playback;
static int video_gui_buf_index;                      // Index into file_gui_buffer ping-pong buffer
static uint32_t video_len_msec;                      // Length of the video
static uint32_t video_delay_msec;                    // Delay between last image and next
static uint64_t video_start_img_msec;                // Timestamp for first image in video
static uint64_t video_cur_img_msec;                  // Timestamp for current (displayed) image
static uint64_t video_last_sys_msec;                 // System timestamp of last image sent
       
// Filesystem catalog information for CMD/RSP and GUI
//  - Used to synchronize between this task and gui/cmd tasks
//  - Statically allocated buffer size based on longest possible name type
static int catalog_type[2];                            // -1 is list of directories
static int num_catalog_names[2];                       // Set with catalog_names_buffer
static char catalog_names_buffer[2][FILE_MAX_CATALOG_NAMES * FILE_NAME_LEN];



//
// File Task Forward Declarations for internal functions
//
static void init_task();
static void handle_notifications();
static void update_card_present_info();
static void catalog_filesystem();
static bool delete_image(int src);
static bool delete_directory(int src);
static bool format_card(int src);
static void setup_delete_image(int src);
static bool setup_store_image();
static bool setup_recording();
static bool stop_recording();
static void eval_record_ready();
static void save_image(int n);
static bool write_image_file(int n);
static bool write_json_buffer(char* buf, int buf_len);
static void close_open_write_file(bool err);
static bool get_json_time_date(char* src, int len, tmElements_t* te);
static bool copy_date_time(char* src, char* dst, int max);
static bool read_image(int dst);
static bool setup_playback(int dst);
static bool start_gui_playback(bool* eof);
static void pause_gui_playback();
static void stop_playback(int dst);
static bool eval_rsp_playback(bool* eof);
static bool eval_gui_playback(bool* eof);
static bool read_json_record(int dst, bool is_img, bool* eof);
static void setup_read_ping_pong(int dst);
static void close_open_read_file(int dst);
static int string_to_read_json_obj(char* s);
static void free_read_json_obj();


//
// File Task API
//
void file_task()
{
	bool eof;                        // Set when processing is done while reading a file
	bool fast_eval;                  // Set when we're processing an on-going activity
	
	ESP_LOGI(TAG, "Start task");
	
	init_task();
	
	// Try to initialize a SD Card if one is there.
	if (power_get_sdcard_present()) {
		if (file_init_card()) {
			ESP_LOGI(TAG, "SD Card found");
			// Notify app_task we have an SD Card
			xTaskNotify(task_handle_app, APP_NOTIFY_SDCARD_PRESENT_MASK, eSetBits);
			
			// Mount it briefly to force a format if necessary and create an initial
			// filesystem information structure (catalog), then unmount it.
			if (file_mount_sdcard()) {
				catalog_filesystem();
				file_unmount_sdcard();
				card_present = true;
			} else {
				ESP_LOGI(TAG, "SD Card could not be mounted");
				card_present = false;
			}
		} else {
			xTaskNotify(task_handle_app, APP_NOTIFY_SDCARD_MISSING_MASK, eSetBits);
			ESP_LOGI(TAG, "SD Card could not be initialized");
			card_present = false;
		}
	} else {
		xTaskNotify(task_handle_app, APP_NOTIFY_SDCARD_MISSING_MASK, eSetBits);
		ESP_LOGI(TAG, "No SD Card found");
		card_present = false;
	}
	
	// Loop handling notifications and file operation requests
	while (1) {
		fast_eval = false;
		
		// Evaluate recording conditions for ready to save image if enabled before
		// handling notifications (of images from lep_task)
		if (recording) {
			fast_eval = true;
			eval_record_ready();
		}
		
		handle_notifications();
		
		if (!rec_file_open && !read_file_open[FILE_REQ_SRC_CMD] && !read_file_open[FILE_REQ_SRC_GUI]) {
			update_card_present_info();
		}
		
		// Evaluate saving
		if (got_lep_image_0 || got_lep_image_1) {
			if (got_lep_image_0) {
				save_image(0);
				got_lep_image_0 = false;
			} else {
				save_image(1);
				got_lep_image_1 = false;
			}
			
			// Stop lep_task from sending us images if necessary (take picture/single image)
			if (!recording) {
				xTaskNotify(task_handle_lep, LEP_NOTIFY_DIS_FILE_FRAME_MASK, eSetBits);
			}
		}
		
		// Evaluate playback
		if (read_file_open[FILE_REQ_SRC_CMD]) {
			if (!eval_rsp_playback(&eof)) {
				if (eof) {
					xTaskNotify(task_handle_rsp, RSP_NOTIFY_FILE_VID_END_MASK, eSetBits);
				} else {
					xTaskNotify(task_handle_app, APP_NOTIFY_PB_CMD_FAIL_MASK, eSetBits);
				}
				stop_playback(FILE_REQ_SRC_CMD);
			}
		}
		if (read_file_open[FILE_REQ_SRC_GUI] && video_playing) {
			if (eval_gui_playback(&eof)) {
				fast_eval = true;
			} else {
				if (eof) {
					xTaskNotify(task_handle_gui, GUI_NOTIFY_FILE_PB_DONE_MASK, eSetBits);
				} else {
					xTaskNotify(task_handle_app, APP_NOTIFY_PB_GUI_FAIL_MASK, eSetBits);
				}
				stop_playback(FILE_REQ_SRC_GUI);
			}
		}
	
		// Sleep task
		if (fast_eval) {
			vTaskDelay(pdMS_TO_TICKS(FILE_TASK_EVAL_FAST_MSEC));
		} else {
			vTaskDelay(pdMS_TO_TICKS(FILE_TASK_EVAL_NORM_MSEC));
		}
	}
}


// Called by other tasks so HW card present and catalog present remain synced
// We don't protect it since it's a boolean...
bool file_card_present()
{
	return card_present;
}


// Called before sending FILE_NOTIFY_START_RECORDING_MASK
void file_set_record_parameters(uint32_t delay_ms, uint32_t num_frames)
{
	next_record_frame_delay_msec = delay_ms;
	next_record_frame_num = num_frames;
}


// Called by another task before sending FILE_NOTIFY_GET_CATALOG.
void file_set_catalog_index(int src, int type)
{
	catalog_type[src] = type;
}


// Called by another task to get the list as a comma separated string
char* file_get_catalog(int src, int* num, int* type)
{
	*num = num_catalog_names[src];
	*type = catalog_type[src];
	return &catalog_names_buffer[src][0];
}


// Called by another task to setup a read of file
void file_set_get_image(int src, char* dir_name, char* file_name)
{
	strcpy(&read_dir_names[src][0], dir_name);
	strcpy(&read_file_names[src][0], file_name);
}


// Called by another task to setup the deletion of a directory
void file_set_del_dir(int src, char* dir_name)
{
	strcpy(&del_dir_names[src][0], dir_name);
}


// Called by another task to setup the deletion of a file
void file_set_del_image(int src, char* dir_name, char* file_name)
{
	strcpy(&del_dir_names[src][0], dir_name);
	strcpy(&del_file_names[src][0], file_name);
}


// Called by rsp_task to get a pointer to the current rsp_file_text ping-pong buffer
// side being read.
char* file_get_rsp_file_text(int* len)
{
	*len = cur_pp_length[FILE_REQ_SRC_CMD][cur_pp_read_index[FILE_REQ_SRC_CMD]];
						
	return rsp_file_text[cur_pp_read_index[FILE_REQ_SRC_CMD]];
}



//
// File Task internal functions
//

/**
 * Initialize
 */
static void init_task()
{
	rec_file_open = false;
	read_file_open[0] = false;
	read_file_open[1] = false;
	read_json_obj = NULL;
	got_lep_image_0 = false;
	got_lep_image_1 = false;
	recording = false;
	rec_image_ready = false;
	next_record_frame_delay_msec = 0;
	next_record_frame_num = 0;
	rsp_ready_for_video_image = false;
	video_playing = false;
	video_gui_buf_index = 0;
}


/**
 * Process notifications from other tasks
 */
static void handle_notifications()
{
	bool eof;
	uint32_t notification_value;
	
	notification_value = 0;
	if (xTaskNotifyWait(0x00, 0xFFFFFFFF, &notification_value, 0)) {
		//
		// FILE MANAGEMENT
		//
		if (Notification(notification_value, FILE_NOTIFY_CMD_DEL_FILE_MASK)) {
			setup_delete_image(FILE_REQ_SRC_CMD);
			if (delete_image(FILE_REQ_SRC_CMD)) {
				xTaskNotify(task_handle_app, APP_NOTIFY_CMD_DEL_SUCCESS_MASK, eSetBits);
			} else {
				xTaskNotify(task_handle_app, APP_NOTIFY_CMD_DEL_FAIL_MASK, eSetBits);
			}
		}
		
		if (Notification(notification_value, FILE_NOTIFY_GUI_DEL_FILE_MASK)) {
			// Must check if we're playing this file (to gui or rsp) and close it first
			// Must update catalog - including delete directory if this was the last file
			setup_delete_image(FILE_REQ_SRC_GUI);
			if (delete_image(FILE_REQ_SRC_GUI)) {
				xTaskNotify(task_handle_app, APP_NOTIFY_GUI_DEL_SUCCESS_MASK, eSetBits);
			} else {
				xTaskNotify(task_handle_app, APP_NOTIFY_GUI_DEL_FAIL_MASK, eSetBits);
			}
		}
		
		if (Notification(notification_value, FILE_NOTIFY_CMD_DEL_DIR_MASK)) {
			if (delete_directory(FILE_REQ_SRC_CMD)) {
				xTaskNotify(task_handle_app, APP_NOTIFY_CMD_DEL_SUCCESS_MASK, eSetBits);
			} else {
				xTaskNotify(task_handle_app, APP_NOTIFY_CMD_DEL_FAIL_MASK, eSetBits);
			}
		}
		
		if (Notification(notification_value, FILE_NOTIFY_GUI_DEL_DIR_MASK)) {
			if (delete_directory(FILE_REQ_SRC_GUI)) {
				xTaskNotify(task_handle_app, APP_NOTIFY_GUI_DEL_SUCCESS_MASK, eSetBits);
			} else {
				xTaskNotify(task_handle_app, APP_NOTIFY_GUI_DEL_FAIL_MASK, eSetBits);
			}
		}
		
		if (Notification(notification_value, FILE_NOTIFY_CMD_FORMAT_MASK)) {
			if (format_card(FILE_REQ_SRC_CMD)) {
				xTaskNotify(task_handle_app, APP_NOTIFY_CMD_DEL_SUCCESS_MASK, eSetBits);
			} else {
				xTaskNotify(task_handle_app, APP_NOTIFY_CMD_DEL_FAIL_MASK, eSetBits);
			}
		}
		
		if (Notification(notification_value, FILE_NOTIFY_GUI_FORMAT_MASK)) {
			if (format_card(FILE_REQ_SRC_GUI)) {
				xTaskNotify(task_handle_app, APP_NOTIFY_GUI_DEL_SUCCESS_MASK, eSetBits);
			} else {
				xTaskNotify(task_handle_app, APP_NOTIFY_GUI_DEL_FAIL_MASK, eSetBits);
			}
		}
		
		if (Notification(notification_value, FILE_NOTIFY_CMD_GET_CATALOG_MASK)) {
			num_catalog_names[FILE_REQ_SRC_CMD] = file_get_name_list(catalog_type[FILE_REQ_SRC_CMD], &catalog_names_buffer[FILE_REQ_SRC_CMD][0]);
			xTaskNotify(task_handle_rsp, RSP_NOTIFY_FILE_CATALOG_READY_MASK, eSetBits);
		}
		
		if (Notification(notification_value, FILE_NOTIFY_GUI_GET_CATALOG_MASK)) {
			num_catalog_names[FILE_REQ_SRC_GUI] = file_get_name_list(catalog_type[FILE_REQ_SRC_GUI], &catalog_names_buffer[FILE_REQ_SRC_GUI][0]);
			xTaskNotify(task_handle_gui, GUI_NOTIFY_FILE_CATALOG_READY_MASK, eSetBits);
		}
		
		//
		// LEPTON
		//
		if (Notification(notification_value, FILE_NOTIFY_LEP_FRAME_MASK_1)) {
			if (rec_image_ready) {
				got_lep_image_0 = true;
				rec_image_ready = false;
			}
		}
		if (Notification(notification_value, FILE_NOTIFY_LEP_FRAME_MASK_2)) {
			if (rec_image_ready) {
				got_lep_image_1 = true;
				rec_image_ready = false;
			}
		}
		
		//
		// IMAGE RECORDING
		//
		if (Notification(notification_value, FILE_NOTIFY_STORE_IMAGE_MASK)) {
			// Setup to store an image
			if (setup_store_image()) {
				// Start images from lep_task
				xTaskNotify(task_handle_lep, LEP_NOTIFY_EN_FILE_FRAME_MASK, eSetBits);
			} else {
				xTaskNotify(task_handle_app, APP_NOTIFY_RECORD_FAIL_MASK, eSetBits);
			}
		}

		if (Notification(notification_value, FILE_NOTIFY_START_RECORDING_MASK)) {
			// Setup to store a series of images to a single file
			if (!setup_recording()) {
				xTaskNotify(task_handle_app, APP_NOTIFY_RECORD_FAIL_MASK, eSetBits);
			}
		}

		if (Notification(notification_value, FILE_NOTIFY_STOP_RECORDING_MASK)) {
			if (recording) {
				if (!stop_recording()) {
					xTaskNotify(task_handle_app, APP_NOTIFY_RECORD_FAIL_MASK, eSetBits);
				}
			}
		}
		
		//
		// IMAGE VIEWING
		//
		// Process END_VIDEO first in case GUI is closing a video before (immediately)
		// asking for the next image or video
		if (Notification(notification_value, FILE_NOTIFY_CMD_END_VIDEO_MASK)) {
			stop_playback(FILE_REQ_SRC_CMD);
		}
		
		if (Notification(notification_value, FILE_NOTIFY_GUI_END_VIDEO_MASK)) {
			stop_playback(FILE_REQ_SRC_GUI);
		}
		
		if (Notification(notification_value, FILE_NOTIFY_CMD_GET_IMAGE_MASK)) {
			// Read the image into the first half of the cmd ping-pong buffer
			if (read_image(FILE_REQ_SRC_CMD)) {
				// Notify rsp_task the file data is available
				xTaskNotify(task_handle_rsp, RSP_NOTIFY_FILE_IMG_READY_MASK, eSetBits);
			} else {
				xTaskNotify(task_handle_app, APP_NOTIFY_PB_CMD_FAIL_MASK, eSetBits);
			}
		}
		
		if (Notification(notification_value, FILE_NOTIFY_GUI_GET_IMAGE_MASK)) {
			if (read_image(FILE_REQ_SRC_GUI)) {
				// Convert the image into lepton data and notify the GUI to display it
				if (string_to_read_json_obj(gui_file_text[0]+1) == FILE_JSON_IMAGE) {
					if (json_parse_image(read_json_obj, &video_cur_img_msec, &file_gui_buffer[video_gui_buf_index])) {
						xTaskNotify(task_handle_gui, GUI_NOTIFY_FILE_IMAGE_READY_MASK, eSetBits);
					}
				} else {
					ESP_LOGE(TAG, "Could not decode image file");
					xTaskNotify(task_handle_app, APP_NOTIFY_PB_GUI_FAIL_MASK, eSetBits);
				}
				free_read_json_obj();
			} else {
				xTaskNotify(task_handle_app, APP_NOTIFY_PB_GUI_FAIL_MASK, eSetBits);
			}
		}
		
		if (Notification(notification_value, FILE_NOTIFY_CMD_GET_VIDEO_MASK)) {
			if (!setup_playback(FILE_REQ_SRC_CMD)) {
				xTaskNotify(task_handle_app, APP_NOTIFY_PB_CMD_FAIL_MASK, eSetBits);
			}
		}
		
		if (Notification(notification_value, FILE_NOTIFY_GUI_GET_VIDEO_MASK)) {
			if (setup_playback(FILE_REQ_SRC_GUI)) {
				// Let the GUI know how long the video is
				gui_set_playback_ts(video_len_msec);
				xTaskNotify(task_handle_gui, GUI_NOTIFY_FILE_UPDATE_PB_LEN_MASK, eSetBits);
			} else {
				xTaskNotify(task_handle_app, APP_NOTIFY_PB_GUI_FAIL_MASK, eSetBits);
			}
		}
		
		if (Notification(notification_value, FILE_NOTIFY_GUI_PLAY_VIDEO_MASK)) {
			if (read_file_open[FILE_REQ_SRC_GUI]) {
				if (!start_gui_playback(&eof)) {
					if (eof) {
						xTaskNotify(task_handle_gui, GUI_NOTIFY_FILE_PB_DONE_MASK, eSetBits);
					} else {
						xTaskNotify(task_handle_app, APP_NOTIFY_PB_GUI_FAIL_MASK, eSetBits);
					}
					stop_playback(FILE_REQ_SRC_GUI);
				}
			} else {
				xTaskNotify(task_handle_gui, GUI_NOTIFY_FILE_PB_DONE_MASK, eSetBits);
			}
		}
		
		if (Notification(notification_value, FILE_NOTIFY_GUI_PAUSE_VIDEO_MASK)) {
			pause_gui_playback();
		}
		
		if (Notification(notification_value, FILE_NOTIFY_RSP_VID_READY_MASK)) {
			// Point to the next read ping-pong buffer
			cur_pp_read_index[FILE_REQ_SRC_CMD] = (cur_pp_read_index[FILE_REQ_SRC_CMD] == 0) ? 1 : 0;
			if (num_pp_valid[FILE_REQ_SRC_CMD] > 0) num_pp_valid[FILE_REQ_SRC_CMD] -= 1;
			rsp_ready_for_video_image = true;
		}
	}
}


/**
 * Handle card insertion/removal detection.  Initialize the a new card.  Update the
 * card present status available from file_utilities and notify the app_task of changes.
 */
static void update_card_present_info()
{
	if (--card_check_count == 0) {
		if (power_get_sdcard_present()) {
			if (!card_present) {
				// Card has just shown up, see if we can initialize it
				if (file_reinit_card()) {
					xTaskNotify(task_handle_app, APP_NOTIFY_SDCARD_PRESENT_MASK, eSetBits);
					ESP_LOGI(TAG, "SD Card detected inserted");
					
					// Mount it briefly to force a format if necessary and create an initial
					// filesystem information structure (catalog), then unmount it.
					if (file_mount_sdcard()) {
						catalog_filesystem();
						file_unmount_sdcard();
					}
					card_present = true;
				}
			}
		} else {
			if (card_present) {
				// Card just removed, clear memory of it
				init_task();   // Reset ourselves
				file_delete_filesystem_info();  // Delete the filesystem information structure (catalog)
				xTaskNotify(task_handle_app, APP_NOTIFY_SDCARD_MISSING_MASK, eSetBits);
				ESP_LOGI(TAG, "SD Card detected removed");
				card_present = false;
			}
		}
		
		// Reset timer
		card_check_count = FILE_CARD_CHECK_PERIOD_MSEC / FILE_TASK_EVAL_NORM_MSEC;
	}
}


/**
 * Generate the filesystem information structure.  Sends an event to app_task if 
 * the catalog was successfully created so that the system can use the filesystem.
 */
static void catalog_filesystem()
{
	if (file_create_filesystem_info()) {
		// Notify app_task it can use filesystem
		xTaskNotify(task_handle_app, APP_NOTIFY_FILESYSTEM_READY_MASK, eSetBits);
		ESP_LOGI(TAG, "Filesystem catalog created");
	} else {
		ESP_LOGE(TAG, "Could not index filesystem");
		rsp_set_cam_info_msg(RSP_INFO_INT_ERROR, "Could not index filesystem");
	}
}


/**
 * Delete the file specified by the directory/file names previously loaded by src
 */
static bool delete_image(int src)
{
	bool success = false;
	directory_node_t* dir;
	int dir_index;
	int file_index;
	
	dir_index = file_get_named_directory_index(&del_dir_names[src][0]);
	if (dir_index != -1) {
		dir = file_get_indexed_directory(dir_index);
		file_index = file_get_named_file_index(dir, &del_file_names[src][0]);
		
		if (file_index != -1) {
			if (!file_get_card_mounted()) {
				(void) file_mount_sdcard();
			}
			// Attempt to mount the filesystem
			if (file_get_card_mounted()) {
				// Attempt to delete the file.  Update the filesystem information structure if successful.
				if (file_delete_file(del_dir_names[src], &del_file_names[src][0])) {
					// Update the filesystem information structure
					file_delete_file_info(dir, file_index);
					
					// Delete the directory itself if it is now empty
					if (dir->num_files == 0) {
						if (file_delete_directory(&del_dir_names[src][0])) {
							file_delete_directory_info(dir_index);
							success = true;
						}
					} else {
						success = true;
					}
				}
				
				file_unmount_sdcard();
			}
		}
	}
	
	return success;
}


/**
 * Delete the directory specified by directory name previously loaded by src.  Delete
 * any files in the directory first.  Stop any ongoing playback or recording.
 */
static bool delete_directory(int src)
{
	bool success = false;
	directory_node_t* dir;
	file_node_t* file;
	int dir_index;
	int i;
	
	for (i=0; i<2; i++) {
		if (read_file_open[i]) {
			stop_playback(i);
		}
	}
	
	if (recording) {
		stop_recording();
	}
	
	dir_index = file_get_named_directory_index(&del_dir_names[src][0]);
	if (dir_index != -1) {
		dir = file_get_indexed_directory(dir_index);
		
		// Attempt to mount the filesystem
		if (file_mount_sdcard()) {
			// Check for files
			if (dir->num_files != 0) {
				// Spin through and delete files from end of list to beginning
				for (i=dir->num_files-1; i>=0; i--) {
					file = file_get_indexed_file(dir, i);
					
					// Attempt to delete the file.  Update the filesystem information structure if successful.
					if (file_delete_file(del_dir_names[src], file->nameP)) {
						// Update the filesystem information structure
						file_delete_file_info(dir, i);
					} else {
						// Stop on failure
						break;
					}
				}
			}
			
			// Attempt to delete the directory if possible
			if (dir->num_files == 0) {
				if (file_delete_directory(&del_dir_names[src][0])) {
					file_delete_directory_info(dir_index);
					success = true;
				}
			}
			
			file_unmount_sdcard();
		}
	}
	
	return success;
}


/**
 * Format the storage medium.  Stop any ongoing playback or recording.
 */ 
static bool format_card(int src)
{
	int i;
	
	for (i=0; i<2; i++) {
		if (read_file_open[i]) {
			stop_playback(i);
		}
	}
	
	if (recording) {
		stop_recording();
	}
	
	// Execute the format and delete the filesystem information structure (catalog)
	if (file_format_card()) {
		ESP_LOGI(TAG, "Format SD Card");
		file_delete_filesystem_info();
		return true;
	} else {
		ESP_LOGE(TAG, "Format SD Card failed");
		return false;
	}
	
	// Mount it briefly to update the card statistics
	if (file_mount_sdcard()) {
		file_unmount_sdcard();
	}	
	
	return true;
}


/**
 * Setup to delete a file.  Stop any playback occurring from the file.
 */
static void setup_delete_image(int src)
{
	int i;
	
	for (i=0; i<2; i++) {
		if (read_file_open[i]) {
			if ((strcmp(del_dir_names[src], &read_dir_names[i][0]) == 0) &&
			    (strcmp(del_file_names[src], &read_dir_names[i][0]) == 0)
			   ) {
			   
				// Stop playback if open read file matches delete file
				stop_playback(i);
			}
		}
	}
}


/**
 * Setup to store a single image
 */
static bool setup_store_image()
{
	bool ret = true;
	
	recording = false;
	
	if (recording) {
		stop_recording();
	}
	
	ret = lep_available(); // Don't open file if there's no camera attached
	
	if (ret) {
		if (!file_get_card_mounted()) {
			ret = file_mount_sdcard();
		}
	
		if (ret) {
			if (file_open_image_write_file(false, &rec_fp)) {
				rec_file_open = true;
				
				// Let the App know the filename
				app_set_write_filename(file_get_open_write_filename());
			} else {
				ESP_LOGE(TAG, "Could not open file for writing");
				ret = false;
			}
		} else {
			ESP_LOGE(TAG, "Could not mount the SD Card");
		}
	} else {
		ESP_LOGE(TAG, "No camera for store image");
	}
	
	rec_image_ready = ret;
	
	return ret;
}


/**
 * Setup to start a recording
 */
static bool setup_recording()
{
	bool ret = true;
	
	ret = lep_available(); // Don't open file if there's no camera attached
	
	if (ret) {
		if (!file_get_card_mounted()) {
			ret = file_mount_sdcard();
		}
	
		if (ret) {
			if (file_open_image_write_file(true, &rec_fp)) {
				rec_file_open = true;
				
				// Let the App know the filename and that we're starting to record
				app_set_write_filename(file_get_open_write_filename());
				xTaskNotify(task_handle_app, APP_NOTIFY_RECORD_START_MASK, eSetBits);
				
				// Setup recording
				num_record_frames = 0;
				cur_record_frame_delay_usec = next_record_frame_delay_msec * 1000;
				cur_record_frame_num = next_record_frame_num;
				record_remaining_frames = next_record_frame_num;
				recording = true;
				
				// Request images from lep_task and get a precision timestamp
				xTaskNotify(task_handle_lep, LEP_NOTIFY_EN_FILE_FRAME_MASK, eSetBits);
				record_req_usec = esp_timer_get_time();
				
				ret = true;
			} else {
				ESP_LOGE(TAG, "Could not open file for writing");
				ret = false;
			}
		} else {
			ESP_LOGE(TAG, "Could not mount the SD Card");
		}
	} else {
		ESP_LOGE(TAG, "No camera for recording");
	}
	
	return ret;		
}


/**
 * End a recording
 */
static bool stop_recording()
{
	bool err = false;
	char buf[256];
	int len;
	
	recording = false;
	
	// Create the video_info json record as the final information written to the file
	len = json_get_video_info(buf, rec_start_time, rec_stop_time, num_record_frames);
		
	if (len > 0) {
		// Write the video_info record stripping off the starting delimitor
		err = !write_json_buffer(&buf[1], len-1);
	} else {
		ESP_LOGE(TAG, "Illegal video_info_json_text for sys_image_file_buffer (%d bytes)", len);
        err = true;
	}
	
	close_open_write_file(err);
	
	// Notify app_task we're done
	xTaskNotify(task_handle_app, APP_NOTIFY_RECORD_STOP_MASK, eSetBits);
	
	// Stop images from lep_task
	xTaskNotify(task_handle_lep, LEP_NOTIFY_DIS_FILE_FRAME_MASK, eSetBits);
	
	return !err;
}


/**
 * Evaluate if we're ready to store an image while recording
 */
static void eval_record_ready()
{
	if (cur_record_frame_delay_usec == 0) {
		// Always ready to record an image
		rec_image_ready = true;
	} else {
		// See if it is time to save the next image
		if (esp_timer_get_time() >= (record_req_usec + cur_record_frame_delay_usec)) {
			record_req_usec = record_req_usec + cur_record_frame_delay_usec;
			rec_image_ready = true;
		}
	}
}


/**
 *
 */
static void save_image(int n)
{
	// Store the image to the open file
	if (rec_file_open) {
		if (!write_image_file(n)) {
			// Write failed - abort operation
			xTaskNotify(task_handle_app, APP_NOTIFY_RECORD_FAIL_MASK, eSetBits);
			close_open_write_file(true);
		} else {
			if (recording) {
				// See if we recorded the specified (non-zero) number of frames
				if (cur_record_frame_num != 0) {
					if (--record_remaining_frames == 0) {
						if (!stop_recording()) {
							xTaskNotify(task_handle_app, APP_NOTIFY_RECORD_FAIL_MASK, eSetBits);
						}
					}
				}
			} else {
				// Close the file after a single image
				close_open_write_file(false);
			
				// Signal success
				xTaskNotify(task_handle_app, APP_NOTIFY_RECORD_IMG_DONE_MASK, eSetBits);
			}
		}
	}
}


/**
 * Create and write out an image file from the specified shared image buffer
 */
static bool write_image_file(int n)
{
	bool err = false;
	
	if (recording) {
		// Get the timestamp (used for the video_info record) immediately before creating
		// the json object so its timestamp matches.
		if (num_record_frames == 0) {
			// First frame
			err = !get_json_time_date(lep_file_buffer[n].bufferP + 1, lep_file_buffer[n].length - 1, &rec_start_time);
		} else {
			// Subsequent and possibly final frame
			err = !get_json_time_date(lep_file_buffer[n].bufferP + 1, lep_file_buffer[n].length - 1, &rec_stop_time);
		}
	
		// Increment the frame count
    	num_record_frames = num_record_frames + 1;
	}
	
	// Write the json string (minus the START delimiter but including the END if recording) to the file
	if (!err) {
		if (xSemaphoreTake(lep_file_buffer[n].mutex, portMAX_DELAY)) {
			if (recording) {
				err = !write_json_buffer(lep_file_buffer[n].bufferP + 1, lep_file_buffer[n].length - 1);
			} else {
				err = !write_json_buffer(lep_file_buffer[n].bufferP + 1, lep_file_buffer[n].length - 2);
			}
			xSemaphoreGive(lep_file_buffer[n].mutex);
		} else {
			err = true;
		}
	}
	
	return !err;
}


/**
 * Write the contents of our system allocated json string buffer to the open file
 */
static bool write_json_buffer(char* buf, int buf_len)
{
	bool err = false;
	char* image_json_text;
	int write_ret;
	int len;
	uint32_t image_json_len;
	uint32_t byte_offset;
	
#ifdef LOG_WRITE_TIMESTAMP
	int cnt = 0;
	int64_t tb, te;
	
	tb = esp_timer_get_time();
#endif
	
	image_json_text = buf;
	image_json_len = buf_len;
	byte_offset = 0;
	while (byte_offset < image_json_len) {
		// Determine maximum bytes to write
		len = image_json_len - byte_offset;
		if (len > MAX_FILE_WRITE_LEN) len = MAX_FILE_WRITE_LEN;
		
		write_ret = fwrite(&image_json_text[byte_offset], 1, len, rec_fp);
#ifdef LOG_WRITE_TIMESTAMP
		cnt++;
#endif
		if (write_ret < 0) {
			ESP_LOGE(TAG, "Error in file write - %d", write_ret);
			err = true;
			break;
		}
		byte_offset += write_ret;
	}
	
#ifdef LOG_WRITE_TIMESTAMP
	te = esp_timer_get_time();
	ESP_LOGI(TAG, "write_json_buffer took %d uSec (%d writes)", (int) (te - tb), cnt);
#endif

	return !err;
}


/**
 * Close the open write file updating the filesystem information structure if the
 * write was successful.
 */
static void close_open_write_file(bool err)
{
	bool new_dir;
	char* dir_name;
	char* file_name;
	directory_node_t* dir_node;
	int dir_num;
	
	// Update the filesystem information structure (catalog)
	if (!err) {
		dir_name = file_get_open_write_dirname(&new_dir);
		file_name = file_get_open_write_filename();
		
		if (new_dir) {
			dir_node = file_add_directory_info(dir_name);
		} else {
			dir_num = file_get_named_directory_index(dir_name);
			if (dir_num < 0) dir_num = file_get_num_directories() - 1;
			dir_node = file_get_indexed_directory(dir_num);
		}
		(void) file_add_file_info(dir_node, file_name);
	}
	
	// Close the file
	rec_file_open = false;
	file_close_file(rec_fp);
	
	// Unmount the SD Card if possible
	if (!read_file_open[0] && !read_file_open[1]) {
		file_unmount_sdcard();
	}
}


/**
 * Parse the specified shared image buffer for time and data metadata and fill
 * in the tmElements_t structure.
 */
static bool get_json_time_date(char* src, int len, tmElements_t* te)
{
	char time[14];
	char date[12];
	char* cP;
	int err = 0;
	
	// Make sure the src is null-terminated
	*(src + len) = 0;
	
	// Find and copy the time string - "Time" : "HH:MM:SS:msec"
	//   1. Find the string "Time"
	//   2. Skip two quote characters to get to the start of the time string
	//   3. Copy the time string up to the next quote character
	if ((cP = strstr(src, "Time")) == NULL) {
		err = 1;
	}
	if (!err) if ((cP = strchr(cP, '"')) == NULL) {
		err = 2;
	}
	if (!err) if ((cP = strchr(cP+1, '"')) == NULL) {
		err = 3;
	}
	if (!err) if (!copy_date_time(cP+1, time, 14)) {
		err = 4;
	}
	
	// Find and copy the date string - "Date" : "MM/DD/YY"
	//   1. Find the string "Date"
	//   2. Skip two quote characters to get to the start of the date string
	//   3. Copy the date string up to the next quote character
	if (!err) if ((cP = strstr(src, "Date")) == NULL) {
		err = 5;
	}
	if (!err) if ((cP = strchr(cP, '"')) == NULL) {
		err = 6;
	}
	if (!err) if ((cP = strchr(cP+1, '"')) == NULL) {
		err = 7;
	}
	if (!err) if (!copy_date_time(cP+1, date, 12)) {
		err = 8;
	}
	
	if (!err) {
		// Fill in the tmElements_t structure from the time and date strings
		time_get_time_from_strings(te, time, date);
		return true;
	} else {
		ESP_LOGE(TAG, "get_json_time_date error %d", err);
		return false;
	}
}


/**
 * Copy characters from src to dst until a quote character is found
 */
static bool copy_date_time(char* src, char* dst, int max)
{
	char c;
	
	c = *src++;
	while ((c != '"') && (max-- != 0)) {
		*dst++ = c;
		c = *src++;
	}
	
	if (max == 0) {
		// Didn't find end-of-string - bad string
		return false;
	} else {
		*dst = 0;
		return true;
	}
}


/**
 * Read a single image file into the first half of the ping-pong buffer
 */
static bool read_image(int dst)
{
	bool eof;
	bool ret = true;
	
	// Initialize
	if (read_file_open[dst]) {
		stop_playback(dst);
	}
	setup_read_ping_pong(dst);
	
	if (!file_get_card_mounted()) {
		ret = file_mount_sdcard();
	}
	
	if (ret) {
		if (file_open_image_read_file(read_dir_names[dst], read_file_names[dst], &read_fp[dst])) {
			read_file_open[dst] = true;
			// Attempt to read the [single] image into the first half of our ping-pong buffer
			if (!read_json_record(dst, true, &eof)) {
				ESP_LOGE(TAG, "Could not find valid image in file /%s/%s", read_dir_names[dst], read_file_names[dst]);
				ret = false;
			}
			
			// Close the file
			close_open_read_file(dst);
		} else {
			ESP_LOGE(TAG, "Could not open file for reading");
			ret = false;
		}
	} else {
		ESP_LOGE(TAG, "Could not mount the SD Card");
		ret = false;
	}
	
	return ret;
}


/**
 * Setup to play a video:
 *   - Attempt to open the file
 *   - Attempt to read the end of the file and get video information from the video_info record
 *   - Setup the playback parameters
 *   - Attempt to read the first image into the first half of the ping-pong buffer
 *   - Attempt to read the second image into the second half of the ping-pong buffer
 */
static bool setup_playback(int dst)
{
	bool eof;
	bool ret = true;   // Set false if any activity fails
	char* ppbuf;
	char* rbuf;
	int n;
	int brace_pos;
	int rec_type;
	uint64_t end_msec;
	
	// Initialize
	setup_read_ping_pong(dst);
	
	if (!file_get_card_mounted()) {
		ret = file_mount_sdcard();
	}
	
	// Open the file
	if (ret) {
		if (file_open_image_read_file(read_dir_names[dst], read_file_names[dst], &read_fp[dst])) {
			read_file_open[dst] = true;
		} else {
			ret = false;
		}
	}
	
	// Process the video_info record at the end of the file to setup playback parameters
	// for images going to the gui
	if (ret && (dst == FILE_REQ_SRC_GUI)) {
		n = file_get_open_filelength(read_fp[dst]);
		if (n) {
			// Use the read_buffer to hold the section of the file containing
			// the video_info json string
			rbuf = &read_buffers[FILE_REQ_SRC_GUI][0];
			if (file_read_open_section(read_fp[dst], rbuf, n - VIDEO_INFO_READ_LEN, VIDEO_INFO_READ_LEN)) {
				// Find the json record ending brace and set the character following it to
				// null to accurately terminate the json string
				brace_pos = -1;
				for (n=VIDEO_INFO_READ_LEN-1; n>=0; n--) {
					if (rbuf[n] == '}') {
						brace_pos = n;
						break;
					} else {
						rbuf[n] = 0;
					}
				}
				
				// Find the json record starting brace
				brace_pos = -1;
				for (n=0; n<VIDEO_INFO_READ_LEN; n++) {
					if (rbuf[n] == '{') {
						brace_pos = n;
						break;
					}
				}
				
				// Create the video_info json record
				if (brace_pos != -1) {
					if (string_to_read_json_obj(&rbuf[brace_pos]) == FILE_JSON_VIDEO_INFO) {
						// Parse the video_info object
						if (json_parse_video_info(read_json_obj, &video_start_img_msec, &end_msec, &n)) {
							if (n != 0) {
								// Determine if we will used a fixed playback speed
								video_len_msec = (uint32_t) (end_msec - video_start_img_msec);
								video_fixed_playback = (video_len_msec / n) >= VIDEO_FIXED_PLAYBACK_MSEC;
	#ifdef LOG_VIDEO_TIMING
								ESP_LOGI(TAG, "video len = %d", video_len_msec);
								ESP_LOGI(TAG, "num frames = %d", n);
								ESP_LOGI(TAG, "video_fixed_playback = %d", video_fixed_playback);
	#endif			
							} else {
								ESP_LOGE(TAG, "video_info indicated 0 frames");
								ret = false;
							}
						} else {
							ESP_LOGE(TAG, "Could not parse video_info json object");
							ret = false;
						}
					} else {
						ESP_LOGE(TAG, "Could not convert string into video_info obj: %s", &rbuf[brace_pos]);
						ret = false;
					}
					free_read_json_obj();
				} else {
					ESP_LOGE(TAG, "Could not find video_info record in file");
					ret = false;
				}
			} else {
				ESP_LOGE(TAG, "Could not read video_info from file");
				ret = false;
			}
		} else {
			ESP_LOGE(TAG, "Could not get video file length");
			ret = false;
		}
	}
	
	// Read the first image into the first half of the ping-pong buffer and, for the GUI,
	// setup the display buffer
	if (ret) {
		if (dst == FILE_REQ_SRC_CMD) {
			ppbuf = rsp_file_text[cur_pp_load_index[FILE_REQ_SRC_CMD]];
		} else {
			ppbuf = gui_file_text[cur_pp_load_index[FILE_REQ_SRC_GUI]];
		}
		if (read_json_record(dst, false, &eof)) {
			if (eof) {
				ESP_LOGE(TAG, "Premature end to video file");
				ret = false;
			} else if (dst == FILE_REQ_SRC_GUI) {
				// Verify it is an image, load the display buffer (if needed) and get the timestamp from it
				rec_type = string_to_read_json_obj(ppbuf);
				if (rec_type == FILE_JSON_IMAGE) {
					if (json_parse_image(read_json_obj, &video_cur_img_msec, &file_gui_buffer[video_gui_buf_index])) {
						// Point to the next read ping-pong buffer
						cur_pp_read_index[FILE_REQ_SRC_GUI] = (cur_pp_read_index[FILE_REQ_SRC_GUI] == 0) ? 1 : 0;
						if (num_pp_valid[FILE_REQ_SRC_GUI] > 0) num_pp_valid[FILE_REQ_SRC_GUI] -= 1;
				
						// Increment to next gui buffer
						video_gui_buf_index = (video_gui_buf_index == 0) ? 1 : 0;
					} else {
						ESP_LOGE(TAG, "Could not decode first image in video file");
						ret = false;
					}
				} else {
					ESP_LOGE(TAG, "Illegal first record in video file (%d)", rec_type);
					ret = false;
				}
				free_read_json_obj();
			}
		} else {
			ESP_LOGE(TAG, "Could not find valid record in file /%s/%s", read_dir_names[dst], read_file_names[dst]);
			ret = false;
		}
	}
	
	// Read the second image (or, theoretically, video_info) into the second half of the
	// ping-pong buffer
	if (ret) {
		if (!read_json_record(dst, false, &eof)) {
			ESP_LOGE(TAG, "Could not find second record in file /%s/%s", read_dir_names[dst], read_file_names[dst]);
			ret = false;
		}
	}
	
	if (ret) {
		// Ready for playback!!!
		if (dst == FILE_REQ_SRC_CMD) {
			// Data in response to a command starts streaming immediately
			xTaskNotify(task_handle_rsp, RSP_NOTIFY_FILE_VID_START_MASK, eSetBits);
			xTaskNotify(task_handle_rsp, RSP_NOTIFY_FILE_IMG_READY_MASK, eSetBits);			
		} else {
			// Data for the GUI paints the first image and waits for GUI to start playback
			xTaskNotify(task_handle_gui, GUI_NOTIFY_FILE_IMAGE_READY_MASK, eSetBits);
		}
	} else {
		close_open_read_file(dst);
	}
		
	return ret;
}


/**
 * Start processing a video file.  Designed only to be called from a GUI event to start/resume
 * playing. Returns false on failure or eof.  Sets eof if either the video_info record is found
 * or the real EOF is found.
 */
static bool start_gui_playback(bool* eof)
{
	bool ret = true;
	char* ppbuf;
	int rec_type;
	uint64_t next_ts_msec;
		
	// EOF set true if necessary
	*eof = false;
	
	if (num_pp_valid[FILE_REQ_SRC_GUI] > 0) {
		// Create the json object
		ppbuf = gui_file_text[cur_pp_read_index[FILE_REQ_SRC_GUI]];
		rec_type = string_to_read_json_obj(ppbuf);
		if (rec_type == FILE_JSON_IMAGE) {
			// Create an image
			if (json_parse_image(read_json_obj, &next_ts_msec, &file_gui_buffer[video_gui_buf_index])) {
				// Compute the delay for sending the image
				if (video_fixed_playback) {
					video_delay_msec = VIDEO_FIXED_PLAYBACK_MSEC;
				} else {
					video_delay_msec = (uint32_t) (next_ts_msec - video_cur_img_msec);
					if (video_delay_msec < FILE_TASK_EVAL_FAST_MSEC) {
						video_delay_msec = FILE_TASK_EVAL_FAST_MSEC;
					}
				}
#ifdef LOG_VIDEO_TIMING
				ESP_LOGI(TAG, "video_delay_msec = %d", video_delay_msec);
#endif
				video_last_sys_msec = esp_timer_get_time() / 1000;
				video_cur_img_msec = next_ts_msec;
				
				// Point to the next read ping-pong buffer
				cur_pp_read_index[FILE_REQ_SRC_GUI] = (cur_pp_read_index[FILE_REQ_SRC_GUI] == 0) ? 1 : 0;
				if (num_pp_valid[FILE_REQ_SRC_GUI] > 0) num_pp_valid[FILE_REQ_SRC_GUI] -= 1;
				
				// Increment to next gui buffer
				video_gui_buf_index = (video_gui_buf_index == 0) ? 1 : 0;
				
				// Finally note we're playing back a video
				video_playing = true;
			} else {
				ESP_LOGE(TAG, "Could not decode image in video file");
				ret = false;
			}
		} else if (rec_type == FILE_JSON_VIDEO_INFO) {
			// Done
			*eof = true;
		} else {
			ESP_LOGE(TAG, "Illegal record in video file (%d)", rec_type);
			ret = false;
		}
		free_read_json_obj();
	}
	
	// Get the next record if possible
	if (ret && !*eof) {
		if (!read_json_record(FILE_REQ_SRC_GUI, false, eof)) {
			ESP_LOGE(TAG, "Could not find second record in file /%s/%s", read_dir_names[FILE_REQ_SRC_GUI], read_file_names[FILE_REQ_SRC_GUI]);
			ret = false;
		}
	}
		
	return ret & !*eof;
}


/**
 * Pause processing a video file.  Designed only to be called from a GUI event to pause
 * playing.
 */
static void pause_gui_playback()
{
	video_playing = false;
}


/**
 * Stop processing a video file
 */
static void stop_playback(int dst)
{
	if (dst == FILE_REQ_SRC_GUI) {
		video_playing = false;
	}
	num_pp_valid[dst] = 0;
	close_open_read_file(dst);
}


/**
 * Process video playback for the rsp_task in response to a command
 *  - Send image data to rsp_task as it is ready
 *  - Determine when playback is finished and let rsp_task know
 * Returns false on failure or eof.  Sets eof if either the video_info record is found
 * or the real EOF is found.
 */
static bool eval_rsp_playback(bool* eof)
{
	bool ret = true;
	
	// EOF will be noted if necessary
	*eof = false;
	
	if (rsp_ready_for_video_image) {
		rsp_ready_for_video_image = false;
		
		// Send the last image loaded in the ping-pong buffer
		if (num_pp_valid[FILE_REQ_SRC_CMD] > 0) {
			xTaskNotify(task_handle_rsp, RSP_NOTIFY_FILE_IMG_READY_MASK, eSetBits);
		} else {
			ESP_LOGE(TAG, "Unexpected empty cmd video playback ping-pong buffer");
			ret = false;
		}
		
		// Get the next record if possible
		if (ret && !*eof) {
			if (!read_json_record(FILE_REQ_SRC_CMD, false, eof)) {
				ESP_LOGE(TAG, "Could not find video record in file /%s/%s", read_dir_names[FILE_REQ_SRC_CMD], read_file_names[FILE_REQ_SRC_CMD]);
				ret = false;
			}
		} 
	}
	
	return ret && !*eof;
}


/**
 * Process video playback for the GUI
 *  - Determine if an image should be sent, converting it to lepton data
 *  - Determine when playback is finished
 * Returns false on failure or eof.  Sets eof if either the video_info record is found
 * or the real EOF is found.
 */
static bool eval_gui_playback(bool* eof)
{
	bool ret = true;
	char* ppbuf;
	int rec_type;
	uint64_t cur_sys_msec;
	uint64_t next_ts_msec;
	    
	// EOF set true if necessary
	*eof = false;
	
	// Check for timeout
	cur_sys_msec = esp_timer_get_time() / 1000;
	if (((uint32_t)(cur_sys_msec - video_last_sys_msec)) >= (video_delay_msec - (FILE_TASK_EVAL_FAST_MSEC/2))) {
#ifdef LOG_VIDEO_TIMING
		ESP_LOGI(TAG, "eval_gui_playback delay = %d", (uint32_t)(cur_sys_msec - video_last_sys_msec));
#endif
		// Notify the GUI to display the last image we loaded
		video_last_sys_msec = cur_sys_msec;
		gui_set_playback_ts(video_cur_img_msec - video_start_img_msec);
		xTaskNotify(task_handle_gui, GUI_NOTIFY_FILE_UPDATE_PB_TS_MASK | GUI_NOTIFY_FILE_IMAGE_READY_MASK, eSetBits);
		
		// Process the last image loaded in the ping-pong buffer
		if (num_pp_valid[FILE_REQ_SRC_GUI] > 0) {
			// Create the json object
			ppbuf = gui_file_text[cur_pp_read_index[FILE_REQ_SRC_GUI]];
			rec_type = string_to_read_json_obj(ppbuf);
			if (rec_type == FILE_JSON_IMAGE) {
				// Create an image
				if (json_parse_image(read_json_obj, &next_ts_msec, &file_gui_buffer[video_gui_buf_index])) {
					// Compute the new delay for sending the image
					if (video_fixed_playback) {
						video_delay_msec = VIDEO_FIXED_PLAYBACK_MSEC;
					} else {
						video_delay_msec = (uint32_t) (next_ts_msec - video_cur_img_msec);
						if (video_delay_msec < FILE_TASK_EVAL_FAST_MSEC) {
							video_delay_msec = FILE_TASK_EVAL_FAST_MSEC;
						}
					}
#ifdef LOG_VIDEO_TIMING
					ESP_LOGI(TAG, "video_delay_msec = %d", video_delay_msec);
#endif
					video_cur_img_msec = next_ts_msec;
					
					// Point to the next read ping-pong buffer
					cur_pp_read_index[FILE_REQ_SRC_GUI] = (cur_pp_read_index[FILE_REQ_SRC_GUI] == 0) ? 1 : 0;
					if (num_pp_valid[FILE_REQ_SRC_GUI] > 0) num_pp_valid[FILE_REQ_SRC_GUI] -= 1;
					
					// Increment to next gui buffer
					video_gui_buf_index = (video_gui_buf_index == 0) ? 1 : 0;
				} else {
					ESP_LOGE(TAG, "Could not decode image in video file");
					ret = false;
				}
			} else if (rec_type == FILE_JSON_VIDEO_INFO) {
				// Done
				*eof = true;
			} else {
				ESP_LOGE(TAG, "Illegal record in gui video file (%d)", rec_type);
				ret = false;
			}
			free_read_json_obj();
		} else {
			ESP_LOGE(TAG, "Unexpected empty gui video playback ping-pong buffer");
			ret = false;
		}
	
		// Get the next record if possible
		if (ret && !*eof) {
			if (!read_json_record(FILE_REQ_SRC_GUI, false, eof)) {
				ESP_LOGE(TAG, "Could not find video record in file /%s/%s", read_dir_names[FILE_REQ_SRC_GUI], read_file_names[FILE_REQ_SRC_GUI]);
				ret = false;
			}
		} 
	}
	
	return ret && !*eof;
}


/**
 * read a json record into the ping-pong buffers if possible.  Add CMD_JSON_STRING_STOP
 * to the end of image files (since image files don't have that character at the end).
 */
static bool read_json_record(int dst, bool is_img, bool* eof)
{
	bool done = false;
	char* rbuf;
	char* ppbuf;
	char* term_loc;
	int cur_pp_start;
	int read_ret;
	int len;
	int file_len;
	
#ifdef LOG_READ_TIMESTAMP
	int64_t tb, te;
	
	tb = esp_timer_get_time();
#endif

	// EOF will be set if necessary
	*eof = false;
	
	// Safety check
	if (!read_file_open[dst]) {
		return false;
	}
	
	// Setup buffer pointers
	rbuf = &read_buffers[dst][0];
	if (dst == FILE_REQ_SRC_CMD) {
		ppbuf = rsp_file_text[cur_pp_load_index[FILE_REQ_SRC_CMD]];
	} else {
		ppbuf = gui_file_text[cur_pp_load_index[FILE_REQ_SRC_GUI]];
	}
	
	// Store a CMD_JSON_STRING_START delimiter in the first location
	*ppbuf = CMD_JSON_STRING_START;
	cur_pp_start = 1;
	file_len = 1;
	
	// Read open file until EOF or we find a CMD_JSON_STRING_STOP character (ending a record)
	while (!done) {
		read_ret = fread(rbuf, 1, MAX_FILE_READ_LEN, read_fp[dst]);
		if (read_ret == 0) {
			// At EOF or error
			if (feof(read_fp[dst])) {
				*eof = true;
				if (is_img) {
					// Add the STOP character and a new terminator
					*(ppbuf + file_len) = CMD_JSON_STRING_STOP;
					file_len += 1;
					*(ppbuf + file_len) = 0;
				}
			} else {
				ESP_LOGE(TAG, "Error in file read - %d", ferror(read_fp[dst]));
			}
			break;
		} else if (file_len <= (JSON_MAX_IMAGE_TEXT_LEN - read_ret)) {
			// Add a null the the buffer
			*(rbuf + read_ret) = 0;
			// Search for CMD_JSON_STRING_STOP
			term_loc = strchr(rbuf, CMD_JSON_STRING_STOP);
			if (term_loc == NULL) {
				// Not found : copy whole read buffer to ping-pong buffer and continue
				memcpy(ppbuf + cur_pp_start, rbuf, read_ret);
				cur_pp_start += read_ret;
				file_len += read_ret;
			} else {
				// Record term found : copy up to and including term
				len = term_loc - rbuf + 1;  // Number of valid data chars
				memcpy(ppbuf + cur_pp_start, rbuf, len);
				file_len += len;
				
				// Terminate the string
				*(ppbuf + file_len) = 0;
				
				// Reset the file read position to the start of the next record (past
				// CMD_JSON_STRING_STOP) and note done
				(void) fseek(read_fp[dst], -(read_ret - len), SEEK_CUR);
				done = true;
			}
		} else {
			ESP_LOGE(TAG, "Read image string too large");
			break;
		}
	}
	
	// Store the file length
	cur_pp_length[dst][cur_pp_load_index[dst]] = file_len;

	// Point to the next ping-pong buffer
	cur_pp_load_index[dst] = (cur_pp_load_index[dst] == 0) ? 1 : 0;
	if (num_pp_valid[dst] < 2) num_pp_valid[dst] += 1;
	
#ifdef LOG_READ_TIMESTAMP
	te = esp_timer_get_time();
	ESP_LOGI(TAG, "read_json_record took %d uSec (%d bytes)", (int) (te - tb), file_len);
#endif
	
	// We successfully read a record if we see done.  Assume we successfully read a record
	// if we see eof.
	return (done || *eof);
}


/**
 * Initialize ping-pong access variables for the start of a read operation
 */
static void setup_read_ping_pong(int dst)
{
	cur_pp_load_index[dst] = 0;
	cur_pp_read_index[dst] = 0;
	cur_pp_length[dst][0] = 0;
	cur_pp_length[dst][1] = 0;
	num_pp_valid[dst] = 0;
	if (dst == FILE_REQ_SRC_CMD) {
		rsp_ready_for_video_image = false;
	} else {
		video_gui_buf_index = 0;
	}
}


/**
 * Close the specified read file.  Unmount the media if possible.
 */
static void close_open_read_file(int dst)
{
	if (read_file_open[dst]) {
		read_file_open[dst] = false;
		file_close_file(read_fp[dst]);
	}
	
	if (!rec_file_open && !read_file_open[FILE_REQ_SRC_CMD] && !read_file_open[FILE_REQ_SRC_GUI]) {
		file_unmount_sdcard();
	}
}


/**
 * Attempt to create a json object from a string read from an image file.  Return the
 * object type.  The global read_json_obj points to the new object.  Must call
 * free_read_json_obj to free memory after using.
 */
static int string_to_read_json_obj(char* s)
{
	read_json_obj = json_get_object(s);
	return json_get_file_object_type(read_json_obj);
}


static void free_read_json_obj()
{
	if (read_json_obj != NULL) {
		json_free_object(read_json_obj);
		read_json_obj = NULL;
	}
}