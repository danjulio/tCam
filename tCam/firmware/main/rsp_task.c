/*
 * Response Task
 *
 * Implement the response transmission module under control of the command module.
 * Responsible for sending responses to the connected client.  Sources of responses
 * include the command task, lepton task and file task.
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
#include "cmd_task.h"
#include "gui_task.h"
#include "file_task.h"
#include "lep_task.h"
#include "rsp_task.h"
#include "file_utilities.h"
#include "json_utilities.h"
#include "sys_utilities.h"
#include "upd_utilities.h"
#include "system_config.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include <string.h>
#ifdef SYS_SCREENDUMP_ENABLE
#include "mem_fb.h"
#endif


//
// RSP Task constants
//

// Uncomment to log various image processing timestamps
//#define LOG_IMG_TIMESTAMP
//#define LOG_PROC_TIMESTAMP
//#define LOG_SEND_TIMESTAMP


// FW update state
#define FW_UPD_IDLE    0
#define FW_UPD_REQUEST 1
#define FW_UPD_PROCESS 2

// Screen dump
// Note: Because of a hardwired constant in json_utilities this must be at most half JSON_MAX_IMAGE_TEXT_LEN
#define SD_BYTES_PER_PKT 16384


//
// RSP Task variables
//
static const char* TAG = "rsp_task";

// State
static bool connected;
static bool stream_on;
static bool video_response_in_progress;
static bool image_pending;
static bool got_image_0, got_image_1;
static bool got_file;
#ifdef SYS_SCREENDUMP_ENABLE
static bool screen_dump_in_progress;
static int screen_dump_cur_start;
static char* screen_dump_response_buffer;
#endif

// Stream rate/duration control
static uint32_t next_stream_frame_delay_msec;   // mSec between images; 0 = fast as possible
static uint32_t cur_stream_frame_delay_usec;
static uint32_t next_stream_frame_num;          // Number of frames to stream; 0 = infinite
static uint32_t cur_stream_frame_num;
static uint32_t stream_remaining_frames;        // Remaining frames to stream
static int64_t stream_ready_usec;               // Next ESP32 uSec timestamp to send image

// rsp_task initiated (non-image) json strings
static SemaphoreHandle_t rsp_task_mutex;
static char rsp_task_response_buffer[JSON_MAX_RSP_TEXT_LEN];

// Command Response buffer (holds single responses from the cmd_task)
static char cmd_task_response_buffer[JSON_MAX_RSP_TEXT_LEN];

// Firmware update control
static char fw_update_version[UPD_MAX_VER_LEN];
static int fw_update_state;
static int fw_update_wait_timer;                // Counts down eval intervals waiting for some operation
static int fw_req_length;
static int fw_req_attempt_num;
static int fw_cur_loc;
static int fw_seg_start;
static int fw_seg_length;
static int fw_update_percent;
static SemaphoreHandle_t fw_update_mutex = NULL;



//
// RSP Task Forward Declarations for internal functions
//
static void init_state();
static void eval_stream_ready();
static void handle_notifications();
static void send_image(int n);
static bool process_catalog();
static void push_response(char* buf, uint32_t len);
static void send_response(char* rsp, int len);
static bool cmd_response_available();
static int get_cmd_response();
static char pop_cmd_response_buffer();
static void send_get_fw();
static void compute_update_percent();
#ifdef SYS_SCREENDUMP_ENABLE
static int get_screen_dump_response();
#endif



//
// RSP Task API
//
void rsp_task()
{
	char* file_data;
	int len;
	
	ESP_LOGI(TAG, "Start task");
	
	init_state();
	rsp_task_mutex = xSemaphoreCreateMutex();
	
	while (1) {
		// Evaluate streaming conditions for ready to send image if enabled before
		// handling notifications (of images from lep_task)
		if (stream_on) {
			eval_stream_ready();
		}
		
		// Process notifications from other tasks
		handle_notifications();
		
		// Get our current connection state
		if (cmd_connected()) {
			connected = true;
		} else if (connected) {
			// Clear our state since we are no longer connected
			init_state();
		}
		
		// Look for things to send
		if (got_image_0 || got_image_1) {
			if (connected) {
				if (got_image_0) {
#ifdef LOG_IMG_TIMESTAMP
					ESP_LOGI(TAG, "got image 0");
#endif
					got_image_0 = 0;
					send_image(0);
				}
				if (got_image_1) {
#ifdef LOG_IMG_TIMESTAMP
					ESP_LOGI(TAG, "got image 1");
#endif
					got_image_1 = 0;
					send_image(1);
				}
				
				// If streaming, determine if we have sent the required number of images if necessary
				if (stream_on && (cur_stream_frame_num != 0)) {
					if (--stream_remaining_frames == 0) {
						stream_on = false;
					}
				}
				
				// Stop lep_task sending us images if necessary
				if (!stream_on) {
					xTaskNotify(task_handle_lep, LEP_NOTIFY_DIS_RSP_FRAME_MASK, eSetBits);
				}
			}
		}
		
		if (got_file) {
			got_file = false;
			if (connected) {	
				// Get a pointer to the current rsp_file_text ping-pong buffer read entry
				file_data = file_get_rsp_file_text(&len);
				if (len != 0) {
					send_response(file_data, len);
				}
				// error handling?
				if (video_response_in_progress) {
					// Notify file_task we're ready for the next image
					xTaskNotify(task_handle_file, FILE_NOTIFY_RSP_VID_READY_MASK, eSetBits);
				}
			}
		}
		
		if (fw_update_state != FW_UPD_IDLE) {
			// Look for timeout
			if (--fw_update_wait_timer == 0) {
				if (fw_update_state == FW_UPD_REQUEST) {
					// Request timed out without user confirming to start
					xTaskNotify(task_handle_app, APP_NOTIFY_FW_UPD_DONE, eSetBits);
					rsp_set_cam_info_msg(RSP_INFO_UPD_STATUS, "Firmware update request timed out");
					ESP_LOGI(TAG, "Firmware update request timed out");
					fw_update_state = FW_UPD_IDLE;
				} else if (fw_update_state == FW_UPD_PROCESS) {
					if (++fw_req_attempt_num < FW_REQ_MAX_ATTEMPTS) {
						// Request the segment again
						send_get_fw();
						fw_update_wait_timer = RSP_MAX_FW_UPD_GET_WAIT_MSEC / RSP_TASK_EVAL_NORM_MSEC;
						ESP_LOGI(TAG, "Retry chunk request");
					} else {
						// Give up
						xTaskNotify(task_handle_app, APP_NOTIFY_FW_UPD_FAIL, eSetBits);
						rsp_set_cam_info_msg(RSP_INFO_UPD_STATUS, "Host did not respond to multiple chunk requests");
						ESP_LOGE(TAG, "Host did not respond to multiple chunk requests.  Done.");
						upd_early_terminate();
						fw_update_state = FW_UPD_IDLE;
					}
				}
			}
		}
		
		if (cmd_response_available()) {
			// Get the command response and send it if possible
			len = get_cmd_response();
			if (connected && (len != 0)) {
				send_response(cmd_task_response_buffer, len);
			}
		}
		
#ifdef SYS_SCREENDUMP_ENABLE
		if (screen_dump_in_progress) {
			len = get_screen_dump_response();
			if (len == 0) {
				screen_dump_in_progress = false;
			} else if (connected) {
				send_response(screen_dump_response_buffer, len);
			}
		}
#endif
		
		// Sleep task - less if we are streaming
		if (stream_on) {
			vTaskDelay(pdMS_TO_TICKS(RSP_TASK_EVAL_FAST_MSEC));
		} else {
			vTaskDelay(pdMS_TO_TICKS(RSP_TASK_EVAL_NORM_MSEC));
		}
	} 
}


// Called before sending RSP_NOTIFY_CMD_STREAM_ON_MASK
void rsp_set_stream_parameters(uint32_t delay_ms, uint32_t num_frames)
{
	next_stream_frame_delay_msec = delay_ms;
	next_stream_frame_num = num_frames;
}


// Called before sending RSP_NOTIFY_CAM_INFO_MASK
void rsp_set_cam_info_msg(uint32_t info_value, char* info_string)
{
	int i;
	int len;
	
	xSemaphoreTake(rsp_task_mutex, portMAX_DELAY);
	
	// Create the rsp_task json string
	len = json_get_cam_info(rsp_task_response_buffer, info_value, info_string);
	
	// Atomically load cmd_task_response_buffer
	xSemaphoreTake(sys_cmd_response_buffer.mutex, portMAX_DELAY);
	
	// Only load if there's room for this response
	if (len <= (CMD_RESPONSE_BUFFER_LEN - sys_cmd_response_buffer.length)) {
		for (i=0; i<len; i++) {
			// Push data
			*sys_cmd_response_buffer.pushP = rsp_task_response_buffer[i];
			
			// Increment push pointer
			if (++sys_cmd_response_buffer.pushP >= (sys_cmd_response_buffer.bufferP + CMD_RESPONSE_BUFFER_LEN)) {
				sys_cmd_response_buffer.pushP = sys_cmd_response_buffer.bufferP;
			}
		}
		
		sys_cmd_response_buffer.length += len;
	}
	
	xSemaphoreGive(sys_cmd_response_buffer.mutex);
	
	xSemaphoreGive(rsp_task_mutex);
}


// Called before sending RSP_NOTIFY_FW_UPD_REQ_MASK
void rsp_set_fw_upd_req_info(uint32_t length, char* version)
{
	fw_req_length = length;
	strncpy(fw_update_version, version, UPD_MAX_VER_LEN);
}


// Called before sending RSP_NOTIFY_FW_UPD_SEG_MASK
void rsp_set_fw_upd_seg_info(uint32_t start, uint32_t length)
{
	fw_seg_start = start;
	fw_seg_length = length;
}


int rsp_get_update_percent()
{
	int t;
	
	xSemaphoreTake(fw_update_mutex, portMAX_DELAY);
	t = fw_update_percent;
	xSemaphoreGive(fw_update_mutex);
	
	return t;
}


char* rsp_get_fw_upd_version()
{
	return fw_update_version;
}



//
// Internal functions
//

/**
 * (Re)Initialize
 */
static void init_state()
{
	connected = false;
	stream_on = false;
	video_response_in_progress = false;
	image_pending = false;
	got_image_0 = false;
	got_image_1 = false;
	got_file = false;
	fw_update_state = FW_UPD_IDLE;
	
	// Create the mutex to protect firmware update percent value
	if (fw_update_mutex == NULL) {
		fw_update_mutex = xSemaphoreCreateMutex();
//		xSemaphoreGive(fw_update_mutex);
	}
	
	// Flush the command response buffer
	xSemaphoreTake(sys_cmd_response_buffer.mutex, portMAX_DELAY);
	sys_cmd_response_buffer.length = 0;
	sys_cmd_response_buffer.popP = sys_cmd_response_buffer.pushP;
	xSemaphoreGive(sys_cmd_response_buffer.mutex);
	
#ifdef SYS_SCREENDUMP_ENABLE
	screen_dump_in_progress = false;
	screen_dump_cur_start = 0;
	// Buffer must be large enough for base64 encoded data
	if (screen_dump_response_buffer == NULL) {
		screen_dump_response_buffer = heap_caps_malloc(SD_BYTES_PER_PKT*2, MALLOC_CAP_SPIRAM);
		if (screen_dump_response_buffer == NULL) {
			ESP_LOGE(TAG, "Could not allocate screen dump json buffer");
		}
	}
#endif
}


/**
 * Evaluate stream rate/duration variables to see if it's time to send an image.
 * Assumes stream_on set.
 */
static void eval_stream_ready()
{
	// Determine if we are ready to send the next available image
	if (cur_stream_frame_delay_usec == 0) {
		image_pending = true;
	} else {
		if (esp_timer_get_time() >= stream_ready_usec) {
			image_pending = true;
			stream_ready_usec = stream_ready_usec + cur_stream_frame_delay_usec;
		}
	}
}


/**
 * Handle incoming notifications
 */
static void handle_notifications()
{
	uint32_t notification_value;
	
	notification_value = 0;
	if (xTaskNotifyWait(0x00, 0xFFFFFFFF, &notification_value, 0)) {
		//
		// Handle cmd_task notifications
		//
		if (Notification(notification_value, RSP_NOTIFY_CMD_GET_IMG_MASK)) {
			// Enable lep_task to send us images
			xTaskNotify(task_handle_lep, LEP_NOTIFY_EN_RSP_FRAME_MASK, eSetBits);
			
			// Note to process the next received image
			image_pending = true;
			
			// Stop any on-going streaming
			stream_on = false;
		}
		
		if (Notification(notification_value, RSP_NOTIFY_CMD_STREAM_ON_MASK)) {
			// Setup streaming
			cur_stream_frame_delay_usec = next_stream_frame_delay_msec * 1000;
			cur_stream_frame_num = next_stream_frame_num;
			stream_remaining_frames = next_stream_frame_num;
			
			// Start streaming
			stream_on = true;
			
			// Enable lep_task to send us images
			if (!image_pending) {
				xTaskNotify(task_handle_lep, LEP_NOTIFY_EN_RSP_FRAME_MASK, eSetBits);
				image_pending = true;
			}
			
			// Get a timestamp at start
			stream_ready_usec = esp_timer_get_time();
		}
		
		if (Notification(notification_value, RSP_NOTIFY_CMD_STREAM_OFF_MASK)) {
			// Stop images from lep_task
			xTaskNotify(task_handle_lep, LEP_NOTIFY_DIS_RSP_FRAME_MASK, eSetBits);
			
			// Stop streaming
			stream_on = false;
		}
		
		
		//
		// Handle file_task notifications
		//
		if (Notification(notification_value, RSP_NOTIFY_FILE_CATALOG_READY_MASK)) {
			if (!process_catalog()) {
				rsp_set_cam_info_msg(RSP_INFO_CMD_NACK, "Failed to get filesystem catalog");
			}
		}
		
		if (Notification(notification_value, RSP_NOTIFY_FILE_IMG_READY_MASK)) {
			got_file = true;
		}
		
		if (Notification(notification_value, RSP_NOTIFY_FILE_VID_START_MASK)) {
			video_response_in_progress = true;
		}
		
		if (Notification(notification_value, RSP_NOTIFY_FILE_VID_END_MASK)) {
			video_response_in_progress = false;
		}
		
		//
		// Handle lep_task notifications
		//
		if (Notification(notification_value, RSP_NOTIFY_LEP_FRAME_MASK_1)) {
			if (image_pending) {
				got_image_0 = true;
				image_pending = false;
			}
		}
		if (Notification(notification_value, RSP_NOTIFY_LEP_FRAME_MASK_2)) {
			if (image_pending) {
				got_image_1 = true;
				image_pending = false;
			}
		}
		
		//
		// Handle firmware update notifications
		//
		if (Notification(notification_value, RSP_NOTIFY_FW_UPD_REQ_MASK)) {
			// Disable streaming if it is running
			stream_on = false;
			
			// Notify app_ask a fw udpate has been requested
			xTaskNotify(task_handle_app, APP_NOTIFY_FW_UPD_REQ, eSetBits);
			
			// Set our state and a timer (for the user to allow the update)
			fw_update_wait_timer = RSP_MAX_FW_UPD_REQ_WAIT_MSEC / RSP_TASK_EVAL_NORM_MSEC;
			fw_update_state = FW_UPD_REQUEST;
			ESP_LOGI(TAG, "Request update to v%s : %d bytes", fw_update_version, fw_req_length);
		}
		
		if (Notification(notification_value, RSP_NOTIFY_FW_UPD_SEG_MASK)) {
			if (fw_update_state == FW_UPD_PROCESS) {
				// Ignore duplicate updates (ignore anything but the expected packet)
				if (fw_seg_start == fw_cur_loc) {
					if (upd_process_bytes(fw_seg_start, fw_seg_length, fw_upd_segment)) {
						// Update our count and check for termination
						fw_cur_loc += fw_seg_length;
						compute_update_percent();
						if (fw_cur_loc >= fw_req_length) {
							// Done: Attempt to validate and commit the update in flash
							if (upd_complete()) {
								// Flash updated: Let the host know and reboot
								rsp_set_cam_info_msg(RSP_INFO_UPD_STATUS, "Firmware update success");
								ESP_LOGI(TAG, "Firmware update success");
								xTaskNotify(task_handle_app, APP_NOTIFY_FW_UPD_DONE, eSetBits);
							} else {
								// Flash update failed: Let host know and start error indication
								rsp_set_cam_info_msg(RSP_INFO_UPD_STATUS, "Firmware update validation failed");
								ESP_LOGE(TAG, "Firmware update validation failed");
								xTaskNotify(task_handle_app, APP_NOTIFY_FW_UPD_FAIL, eSetBits);
							}
							fw_update_state = FW_UPD_IDLE;	
						} else {
							// Request the next segment
							fw_req_attempt_num = 0;
							send_get_fw();
							fw_update_wait_timer = RSP_MAX_FW_UPD_GET_WAIT_MSEC / RSP_TASK_EVAL_NORM_MSEC;
							ESP_LOGI(TAG, "Request fw chunk @ %d", fw_cur_loc);
						}
					} else {
						// Flash update failed: Let host know and start error indication
						rsp_set_cam_info_msg(RSP_INFO_UPD_STATUS, "Firmware update flash update failed");
						ESP_LOGE(TAG, "Firmware update flash update failed");
						xTaskNotify(task_handle_app, APP_NOTIFY_FW_UPD_FAIL, eSetBits);
						upd_early_terminate();
						fw_update_state = FW_UPD_IDLE;
					}
				}
			}
		}
		
		if (Notification(notification_value, RSP_NOTIFY_FW_UPD_EN_MASK)) {
			if (fw_update_state == FW_UPD_REQUEST) {
				// Attempt to setup an update
				if (upd_init(fw_req_length, fw_update_version)) {
					// Indicate to the user a fw update is now in process
					xTaskNotify(task_handle_app, APP_NOTIFY_FW_UPD_PROCESS, eSetBits);
				
					// Request first segment / setup timer
					fw_cur_loc = 0;
					fw_req_attempt_num = 0;
					compute_update_percent();
					send_get_fw();
					fw_update_wait_timer = RSP_MAX_FW_UPD_GET_WAIT_MSEC / RSP_TASK_EVAL_NORM_MSEC;
					fw_update_state = FW_UPD_PROCESS;
					
					ESP_LOGI(TAG, "Start update");
				} else {
					// Update init failed: Let host know and start error indication
					rsp_set_cam_info_msg(RSP_INFO_UPD_STATUS, "Firmware update flash init failed");
					ESP_LOGE(TAG, "Firmware update flash init failed");
					xTaskNotify(task_handle_app, APP_NOTIFY_FW_UPD_FAIL, eSetBits);
					fw_update_state = FW_UPD_IDLE;
				}
			}
		}
		
		if (Notification(notification_value, RSP_NOTIFY_FW_UPD_END_MASK)) {
			// Stop the update
			rsp_set_cam_info_msg(RSP_INFO_UPD_STATUS, "Firmware update terminated by user");
			ESP_LOGI(TAG, "Firmware update terminated by user");
			upd_early_terminate();
			fw_update_state = FW_UPD_IDLE;
			
			// Let user know update has stopped
			xTaskNotify(task_handle_app, APP_NOTIFY_FW_UPD_FAIL, eSetBits);
		}
		
#ifdef SYS_SCREENDUMP_ENABLE
		//
		// Screen dump
		//
		if (Notification(notification_value, RSP_NOTIFY_SCREEN_DUMP_START_MASK)) {
			// Trigger the GUI to get a screen capture in the memory frame buffer
			screen_dump_in_progress = false;
			xTaskNotify(task_handle_gui, GUI_NOTIFY_SCREENDUMP_MASK, eSetBits);
			
		}
		if (Notification(notification_value, RSP_NOTIFY_SCREEN_DUMP_AVAIL_MASK)) {
			// Start sending the frame buffer back to the host
			screen_dump_in_progress = true;
			screen_dump_cur_start = 0;
		}
#endif
	}
}


/**
 * Get access to and then send the image from the specified half of the lep_rsp_buffer
 */
static void send_image(int n)
{
	// Get access to the buffer
	if (xSemaphoreTake(lep_rsp_buffer[n].mutex, portMAX_DELAY)) {
		// Send the image
		if (lep_rsp_buffer[n].length != 0) {
			send_response(lep_rsp_buffer[n].bufferP, lep_rsp_buffer[n].length);
		}
		
		xSemaphoreGive(lep_rsp_buffer[n].mutex);
	}
}


/**
 * Convert a filelist just created by file_task into a json record for delimiters and push
 * it into our cmd_task_response_buffer
 */
static bool process_catalog()
{
	char* names;
	char dir_name[DIR_NAME_LEN];
	int num_names;
	int dir_num;
	directory_node_t* dir_obj;
	
	// Get a pointer to the comma separated list of names
	names = file_get_catalog(FILE_REQ_SRC_CMD, &num_names, &dir_num);
	
	// Get the directory name
	if (dir_num == -1) {
		strcpy(dir_name, "/");
	} else {
		dir_obj = file_get_indexed_directory(dir_num);
		strcpy(dir_name, dir_obj->nameP);
	}
	
	// Create and push the json response
	sys_response_rsp_buffer.length = json_get_filesystem_list_response(sys_response_rsp_buffer.bufferP, dir_name, names);
	if (sys_response_rsp_buffer.length != 0) {
		push_response(sys_response_rsp_buffer.bufferP, sys_response_rsp_buffer.length);
		return true;
	} else {
		return false;
	}
}


/**
 * Push a response into the cmd_task_response_buffer if there is room, otherwise
 * just drop it (up to the external host to make sure this doesn't happen)
 */
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


/**
 * Send a response
 */
static void send_response(char* rsp, int rsp_length)
{
	int byte_offset;
	int err;
	int len;
	int sock;
#ifdef LOG_SEND_TIMESTAMP
	int64_t tb, te;
	
	tb = esp_timer_get_time();
#endif
	
	sock = cmd_get_socket();
	
	// Write our response to the socket
    byte_offset = 0;
	while (byte_offset < rsp_length) {
		len = rsp_length - byte_offset;
		if (len > RSP_MAX_TX_PKT_LEN) len = RSP_MAX_TX_PKT_LEN;
		err = send(sock, rsp + byte_offset, len, 0);
		if (err < 0) {
			ESP_LOGE(TAG, "Error in socket send: errno %d", errno);
			break;
		}
		byte_offset += err;
	}
	
#ifdef LOG_SEND_TIMESTAMP
	te = esp_timer_get_time();
	ESP_LOGI(TAG, "send_response took %d uSec", (int) (te - tb));
#endif
}


/**
 * Atomically check if there is a response from cmd_task to transmit
 */
static bool cmd_response_available()
{
	int len;
	
	xSemaphoreTake(sys_cmd_response_buffer.mutex, portMAX_DELAY);
	len = sys_cmd_response_buffer.length;
	xSemaphoreGive(sys_cmd_response_buffer.mutex);
	
	return (len != 0);
}


/**
 * Load our cmd_task_response_buffer and atomically update the command response buffer
 * indicating we popped a response
 */
static int get_cmd_response()
{
	char c;
	int len = 0;
	
	// Pop an entire delimited json string
	do {
		c = pop_cmd_response_buffer();
		cmd_task_response_buffer[len++] = c;
	} while ((c != CMD_JSON_STRING_STOP) && (len <= JSON_MAX_RSP_TEXT_LEN));
	
	// Atomically update cmd_task_response_buffer
	xSemaphoreTake(sys_cmd_response_buffer.mutex, portMAX_DELAY);
	if (len > JSON_MAX_RSP_TEXT_LEN) {
		// Didn't find complete json string so flush the queue
		sys_cmd_response_buffer.length = 0;
		sys_cmd_response_buffer.popP = sys_cmd_response_buffer.pushP;
		len = 0;
	} else {
		// Subtract the length of the data we popped
		sys_cmd_response_buffer.length = sys_cmd_response_buffer.length - len;
	}
	xSemaphoreGive(sys_cmd_response_buffer.mutex);
	
	return len;
}


/**
 * Pop a character from the command response buffer
 */
static char pop_cmd_response_buffer()
{
	char c;
	
	c = *sys_cmd_response_buffer.popP;
	
	if (++sys_cmd_response_buffer.popP >= (sys_cmd_response_buffer.bufferP + CMD_RESPONSE_BUFFER_LEN)) {
		sys_cmd_response_buffer.popP = sys_cmd_response_buffer.bufferP;
	}
	
	return c;
}


/**
 * Push a get_fw packet into our own queue with the current segment to get
 */
static void send_get_fw()
{
	int i;
	int len;
	uint32_t get_fw_length;
	
	// Compute the length of this request
	if ((fw_req_length - fw_cur_loc) > FW_UPD_CHUNK_MAX_LEN) {
		get_fw_length = FW_UPD_CHUNK_MAX_LEN;
	} else {
		get_fw_length = fw_req_length - fw_cur_loc;
	}
	
	xSemaphoreTake(rsp_task_mutex, portMAX_DELAY);
	
	// Create the rsp_task json string
	len = json_get_get_fw(rsp_task_response_buffer, fw_cur_loc, get_fw_length);
	
	// Atomically load cmd_task_response_buffer
	xSemaphoreTake(sys_cmd_response_buffer.mutex, portMAX_DELAY);
	
	// Only load if there's room for this response
	if (len <= (CMD_RESPONSE_BUFFER_LEN - sys_cmd_response_buffer.length)) {
		for (i=0; i<len; i++) {
			// Push data
			*sys_cmd_response_buffer.pushP = rsp_task_response_buffer[i];
			
			// Increment push pointer
			if (++sys_cmd_response_buffer.pushP >= (sys_cmd_response_buffer.bufferP + CMD_RESPONSE_BUFFER_LEN)) {
				sys_cmd_response_buffer.pushP = sys_cmd_response_buffer.bufferP;
			}
		}
		
		sys_cmd_response_buffer.length += len;
	}
	
	xSemaphoreGive(sys_cmd_response_buffer.mutex);
	
	xSemaphoreGive(rsp_task_mutex);
}


static void compute_update_percent()
{
	int percent;
	
	if (fw_req_length == 0) {
		percent = 0;
	} else {
		percent = fw_cur_loc * 100 / fw_req_length;
	}
	
	xSemaphoreTake(fw_update_mutex, portMAX_DELAY);
	fw_update_percent = percent;
	xSemaphoreGive(fw_update_mutex);
}


#ifdef SYS_SCREENDUMP_ENABLE
static int get_screen_dump_response()
{
	int len = 0;
	int req_len;
	uint8_t* fb;
	
	// Create the screen_dump_response json string
	if (screen_dump_response_buffer != NULL) {
		fb = mem_fb_get_buffer();
		if (screen_dump_cur_start < (MEM_FB_W * MEM_FB_H * (MEM_FB_BPP / 8))) {
			req_len = (MEM_FB_W * MEM_FB_H * (MEM_FB_BPP / 8)) - screen_dump_cur_start;
			if (req_len > SD_BYTES_PER_PKT) req_len = SD_BYTES_PER_PKT;
			len = json_get_dump_screen_response(screen_dump_response_buffer, screen_dump_cur_start, req_len, fb);
			screen_dump_cur_start += req_len;
		}
	}
	
	return len;	
}
#endif
