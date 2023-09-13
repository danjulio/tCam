/*
 * Cmd Task
 *
 * Implement the command processing module including management of the WiFi
 * interface.  Enable mDNS for device discovery.
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
#include "app_task.h"
#include "cmd_task.h"
#include "file_task.h"
#include "gui_task.h"
#include "rsp_task.h"
#include "file_utilities.h"
#include "json_utilities.h"
#include "lepton_utilities.h"
#include "power_utilities.h"
#include "ps_utilities.h"
#include "sys_utilities.h"
#include "time_utilities.h"
#include "upd_utilities.h"
#include "wifi_utilities.h"
#include "system_config.h"
#include "esp_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include <string.h>



//
// CMD Task private constants
//

// Uncomment to print commands
//#define DEBUG_CMD


//
// CMD Task variables
//
static const char* TAG = "cmd_task";

// Client socket
static int client_sock = -1;

// Connected status
static bool connected = false;

// Main receive buffer indicies;
static int rx_circular_push_index;
static int rx_circular_pop_index;



//
// CMD Task Forward Declarations for internal functions
//
static void init_command_processor();
static void push_rx_data(char* data, int len);
static bool process_rx_data();
static void process_rx_packet();
static void push_response(char* buf, uint32_t len);
static void push_lep_command();
static bool process_set_config(cJSON* cmd_args);
static bool process_stream_on(cJSON* cmd_args);
static bool process_record_on(cJSON* cmd_args);
static bool process_set_time(cJSON* cmd_args);
static bool process_set_wifi(cJSON* cmd_args);
static bool process_get_fs_list(cJSON* cmd_args);
static bool process_get_fs_file(cJSON* cmd_args);
static bool process_del_fs_obj(cJSON* cmd_args);
static bool process_fw_upd_request(cJSON* cmd_args);
static bool process_fw_segment(cJSON* cmd_args);
static int in_buffer(char c);
//static void cmd_start_mdns();



//
// CMD Task API
//
void cmd_task()
{
	char rx_buffer[256];
    char addr_str[16];
    int err;
    int flag;
    int len;
    int listen_sock;
    struct sockaddr_in destAddr;
    struct sockaddr_in sourceAddr;
    uint32_t addrLen;
    
	ESP_LOGI(TAG, "Start task");
	
	// Loop to setup socket, wait for connection, handle connection.  Terminates
	// when client disconnects
	
	// Wait until WiFi is connected
	if (!wifi_is_connected()) {
		vTaskDelay(pdMS_TO_TICKS(500));
	}
	
	// Config IPV4
    destAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(CMD_PORT);
    inet_ntoa_r(destAddr.sin_addr, addr_str, sizeof(addr_str) - 1);
        
    // socket - bind - listen - accept
    listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        goto error;
    }
    ESP_LOGI(TAG, "Socket created");

	flag = 1;
  	setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    err = bind(listen_sock, (struct sockaddr *)&destAddr, sizeof(destAddr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
         goto error;
    }
    ESP_LOGI(TAG, "Socket bound");
    
	while (1) {
		init_command_processor();
			
        err = listen(listen_sock, 1);
        if (err != 0) {
            ESP_LOGE(TAG, "Error occured during listen: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket listening");
		
        addrLen = sizeof(sourceAddr);
        client_sock = accept(listen_sock, (struct sockaddr *)&sourceAddr, &addrLen);
        if (client_sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket accepted");
        connected = 1;
		
        // Handle communication with client
        while (1) {
        	len = recv(client_sock, rx_buffer, sizeof(rx_buffer) - 1, MSG_DONTWAIT);
            // Error occured during receiving
            if (len < 0) {
            	if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
            		if (wifi_is_connected()) {
            			// Nothing there to receive, so just wait before calling recv again
            			vTaskDelay(pdMS_TO_TICKS(50));
            		} else {
            			ESP_LOGI(TAG, "Closing connection");
            			break;
            		}
            	} else {
                	ESP_LOGE(TAG, "recv failed: errno %d", errno);
                	break;
                }
            }
            // Connection closed
            else if (len == 0) {
                ESP_LOGI(TAG, "Connection closed");
                break;
            }
            // Data received
            else {
            	// Store new data
            	push_rx_data(rx_buffer, len);
        	
            	// Look for and handle commands
            	while (process_rx_data()) {}
            }
        }
        
        // Close this session
        connected = false;
        if (client_sock != -1) {
            ESP_LOGI(TAG, "Shutting down socket and restarting...");
            shutdown(client_sock, 0);
            close(client_sock);
        }
	}

error:
	ESP_LOGI(TAG, "Something went seriously wrong with networking handling - bailing");
	// ??? Eventually add a fault reporting mechanism that will show up in gui
	vTaskDelete(NULL);
}


/**
 * True when connected to a client
 */
bool cmd_connected()
{
	return connected;
}


/**
 * Return socket descriptor
 */
int cmd_get_socket()
{
	return client_sock;
}



//
// CMD Task internal functions
//

/**
 * Initialize variables associated with receiving and processing commands
 */
static void init_command_processor()
{
	rx_circular_push_index = 0;
	rx_circular_pop_index = 0;
}


/**
 * Push received data into our circular buffer
 */
static void push_rx_data(char* data, int len)
{	
	// Push the received data into the circular buffer
	while (len-- > 0) {
		rx_circular_buffer[rx_circular_push_index] = *data++;
		if (++rx_circular_push_index >= JSON_MAX_CMD_TEXT_LEN) rx_circular_push_index = 0;
	}
}


/**
 * See if we can find a complete json string to process
 */
static bool process_rx_data() {
	bool valid_string = false;
	int begin, end, i;
	
	// See if we can find an entire json string
	end = in_buffer(CMD_JSON_STRING_STOP);
	if (end >= 0) {
		// Found end of packet, look for beginning
		begin = in_buffer(CMD_JSON_STRING_START);
		if (begin >= 0) {
			// Found packet - copy it, without delimiters, to json_cmd_string
			//
			// Skip past start
			while (rx_circular_pop_index != begin) {
				if (++rx_circular_pop_index >= JSON_MAX_CMD_TEXT_LEN) rx_circular_pop_index = 0;
			}
			
			// Copy up to end
			i = 0;
			while ((rx_circular_pop_index != end) && (i < JSON_MAX_CMD_TEXT_LEN)) {
				if (i < JSON_MAX_CMD_TEXT_LEN) {
					json_cmd_string[i] = rx_circular_buffer[rx_circular_pop_index];
				}
				i++;
				if (++rx_circular_pop_index >= JSON_MAX_CMD_TEXT_LEN) rx_circular_pop_index = 0;
			}
			json_cmd_string[i] = 0;               // Make sure this is a null-terminated string
			
			// Skip past end
			if (++rx_circular_pop_index >= JSON_MAX_CMD_TEXT_LEN) rx_circular_pop_index = 0;
			
			if (i < JSON_MAX_CMD_TEXT_LEN+1) {
				// Process json command string
				process_rx_packet();
				valid_string = true;
			}
		} else {
			// Unexpected end without start - skip it
			while (rx_circular_pop_index != end) {
				if (++rx_circular_pop_index >= JSON_MAX_CMD_TEXT_LEN) rx_circular_pop_index = 0;
			}
		}
	}
	
	return valid_string;
}


static void process_rx_packet()
{
	cJSON* json_obj;
	cJSON* cmd_args;
	int cmd;
	int cmd_success = -1;  // -1: response sent (as ACK), 0: determined elsewhere, 1: success,
						   //  2: fail, 3: unimplemented, 3-4: unknown cmd, 4-5: unknown json, 5-6: bad json
	char cmd_st_buf[80];
	
	// Create a json object to parse
	json_obj = json_get_object(json_cmd_string);
#ifdef DEBUG_CMD
	ESP_LOGI(TAG, "RX %s", json_cmd_string); 
#endif
	if (json_obj != NULL) {
		if (json_parse_cmd(json_obj, &cmd, &cmd_args)) {
#ifdef DEBUG_CMD
			ESP_LOGI(TAG, "cmd %s", json_get_cmd_name(cmd));
#endif
			switch (cmd) {
				case CMD_GET_STATUS:
					sys_response_cmd_buffer.length = json_get_status(sys_response_cmd_buffer.bufferP);
					if (sys_response_cmd_buffer.length != 0) {
						push_response(sys_response_cmd_buffer.bufferP, sys_response_cmd_buffer.length);
					} else {
						cmd_success = 2;
					}
					break;
					
				case CMD_GET_IMAGE:
					xTaskNotify(task_handle_rsp, RSP_NOTIFY_CMD_GET_IMG_MASK, eSetBits);
					cmd_success = 0;
					break;
					
				case CMD_SET_TIME:					
					if (process_set_time(cmd_args)) {
						cmd_success = 1;
					} else {
						cmd_success = 2;
					}
					break;
				
				case CMD_GET_WIFI:
					sys_response_cmd_buffer.length = json_get_wifi(sys_response_cmd_buffer.bufferP);
					if (sys_response_cmd_buffer.length != 0) {
						push_response(sys_response_cmd_buffer.bufferP, sys_response_cmd_buffer.length);
					} else {
						cmd_success = 2;
					}
					break;
					
				case CMD_SET_WIFI:
					if (process_set_wifi(cmd_args)) {
						cmd_success = 0;
					} else {
						cmd_success = 2;
					}
					break;
				
				case CMD_GET_CONFIG:
					sys_response_cmd_buffer.length = json_get_config(sys_response_cmd_buffer.bufferP);
					if (sys_response_cmd_buffer.length != 0) {
						push_response(sys_response_cmd_buffer.bufferP, sys_response_cmd_buffer.length);
					} else {
						cmd_success = 2;
					}					
					break;
					
				case CMD_SET_CONFIG:
					if (process_set_config(cmd_args)) {
						cmd_success = 1;
					} else {
						cmd_success = 2;
					}
					break;
				
				case CMD_SET_SPOT:
					push_lep_command();
					cmd_success = 1;
					break;
				
				case CMD_STREAM_ON:
					if (process_stream_on(cmd_args)) {
						cmd_success = 1;
					} else {
						cmd_success = 2;
					}
					break;
				
				case CMD_STREAM_OFF:
					xTaskNotify(task_handle_rsp, RSP_NOTIFY_CMD_STREAM_OFF_MASK, eSetBits);
					cmd_success = 1;
					break;
						
				case CMD_TAKE_PIC:
					xTaskNotify(task_handle_app, APP_NOTIFY_CMD_TAKE_PICTURE_MASK, eSetBits);
					cmd_success = 0;
					break;
				
				case CMD_RECORD_ON:
					if (process_record_on(cmd_args)) {
						cmd_success = 0;
					} else {
						cmd_success = 2;
					}
					break;
				
				case CMD_RECORD_OFF:
					xTaskNotify(task_handle_app, APP_NOTIFY_CMD_STOP_RECORD_MASK, eSetBits);
					cmd_success = 0;
					break;
					
				case CMD_POWEROFF:
					xTaskNotify(task_handle_app, APP_NOTIFY_SHUTDOWN_MASK, eSetBits);
					cmd_success = 1;
					break;
				
				case CMD_RUN_FFC:
					lepton_ffc();
					cmd_success = 1;
					break;
				
				case CMD_GET_FS_LIST:
					if (process_get_fs_list(cmd_args)) {
						cmd_success = 0;
					} else {
						cmd_success = 2;
					}
					break;
				
				case CMD_GET_FS_FILE:
					if (process_get_fs_file(cmd_args)) {
						cmd_success = 0;
					} else {
						cmd_success = 2;
					}
					break;
				
				case CMD_DEL_FS_OBJ:
					if (process_del_fs_obj(cmd_args)) {
						cmd_success = 0;
					} else {
						cmd_success = 2;
					}
					break;
				
				case CMD_GET_LEP_CCI:				
				case CMD_SET_LEP_CCI:
					push_lep_command();
					break;
					
				case CMD_FW_UPD_REQ:
					if (process_fw_upd_request(cmd_args)) {
						cmd_success = 1;
					} else {
						cmd_success = 2;
					}
					break;
				
				case CMD_FW_UPD_SEG:
					if (process_fw_segment(cmd_args)) {
						cmd_success = 0; // rsp_task will load success/failed cam_info response
					} else {
						cmd_success = 2;
					}
					break;
					
				case CMD_DUMP_SCREEN:
#ifdef SYS_SCREENDUMP_ENABLE
					xTaskNotify(task_handle_rsp, RSP_NOTIFY_SCREEN_DUMP_START_MASK, eSetBits);
					cmd_success = 0;  // rsp_task will send screen_dump_response
#else
					cmd_success = 3;
#endif
					break;

				default:
					cmd_success = 4;
			}
		} else {
			cmd_success = 5;
		}
		
		json_free_object(json_obj);
	} else {
		cmd_success = 6;
	}
	
	// Determine command response to send
	switch (cmd_success) {
		// case -1 does not send message (command response is message)
		// case 0 "determined later" does not send a message at this point
		case 1:
			sprintf(cmd_st_buf, "%s success", json_get_cmd_name(cmd));
			rsp_set_cam_info_msg(RSP_INFO_CMD_ACK, cmd_st_buf);
			break;
		case 2:
			sprintf(cmd_st_buf, "%s failed", json_get_cmd_name(cmd));
			rsp_set_cam_info_msg(RSP_INFO_CMD_NACK, cmd_st_buf);
			break;
		case 3:
			sprintf(cmd_st_buf, "Unsupported command in json string");
			rsp_set_cam_info_msg(RSP_INFO_CMD_UNIMPL, cmd_st_buf);
			break;
		case 4:
			sprintf(cmd_st_buf, "Unknown command in json string");
			rsp_set_cam_info_msg(RSP_INFO_CMD_UNIMPL, cmd_st_buf);
			break;
		case 5:
			sprintf(cmd_st_buf, "Json string wasn't command");
			rsp_set_cam_info_msg(RSP_INFO_CMD_UNIMPL, cmd_st_buf);
			break;
		case 6:
			sprintf(cmd_st_buf, "Couldn't convert json string");
			rsp_set_cam_info_msg(RSP_INFO_CMD_BAD, cmd_st_buf);
			break;
	}
}


/**
 * Push a response into the sys_cmd_response_buffer if there is room, otherwise
 * just drop it (up to the external host to make sure this doesn't happen)
 */
static void push_response(char* buf, uint32_t len)
{
	int i;
	
	// Atomically load sys_cmd_response_buffer
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
 * Push json_cmd_string with added delimiters into lep_cmd_buffer to forward to
 * tCam-Mini
 */
static void push_lep_command()
{
	char c;
	int i = 0;
	
	// Atomically load sys_cmd_response_buffer
	xSemaphoreTake(lep_cmd_buffer.mutex, portMAX_DELAY);
	
	// Push start
	*lep_cmd_buffer.pushP = CMD_JSON_STRING_START;
	if (++lep_cmd_buffer.pushP >= (lep_cmd_buffer.bufferP + LEP_COMMAND_BUFFER_LEN)) {
		lep_cmd_buffer.pushP = lep_cmd_buffer.bufferP;
	}
	
	// Push json_cmd_string
	while ((c = json_cmd_string[i]) != 0) {
		i += 1;
		*lep_cmd_buffer.pushP = c;
		if (++lep_cmd_buffer.pushP >= (lep_cmd_buffer.bufferP + LEP_COMMAND_BUFFER_LEN)) {
			lep_cmd_buffer.pushP = lep_cmd_buffer.bufferP;
		}
	}
	
	// Push end
	*lep_cmd_buffer.pushP = CMD_JSON_STRING_STOP;
	if (++lep_cmd_buffer.pushP >= (lep_cmd_buffer.bufferP + LEP_COMMAND_BUFFER_LEN)) {
		lep_cmd_buffer.pushP = lep_cmd_buffer.bufferP;
	}
	
	lep_cmd_buffer.length += i + 2;
	
	xSemaphoreGive(lep_cmd_buffer.mutex);
}


/**
 * Routines to process commands
 */
static bool process_set_config(cJSON* cmd_args)
{
	lep_config_t new_config_st;
	lep_config_t* lep_stP;
	
	lep_stP = lep_get_lep_st();
	
	if (json_parse_set_config(cmd_args, &new_config_st)) {
		// Look for changed items that require updating other modules
		if (new_config_st.agc_set_enabled != lep_stP->agc_set_enabled) {
			lep_stP->agc_set_enabled = new_config_st.agc_set_enabled;
			xTaskNotify(task_handle_gui, GUI_NOTIFY_NEW_AGC_MASK, eSetBits);
		}
		if (new_config_st.emissivity != lep_stP->emissivity) {
			lep_stP->emissivity = new_config_st.emissivity;
		}
		if (new_config_st.gain_mode != lep_stP->gain_mode) {
			lep_stP->gain_mode = new_config_st.gain_mode;
		}
		// Forward the command on to tCam-Mini
		push_lep_command();
		
		// And save its state in our persistent storage
		ps_set_lep_state(lep_stP);
		return true;
	}
	
	return false;
}


static bool process_stream_on(cJSON* cmd_args)
{
	uint32_t delay_ms, num_frames;
	
	if (json_parse_stream_on(cmd_args, &delay_ms, &num_frames)) {
		rsp_set_stream_parameters(delay_ms, num_frames);
		xTaskNotify(task_handle_rsp, RSP_NOTIFY_CMD_STREAM_ON_MASK, eSetBits);
		return true;
	}
	
	return false;
}


static bool process_record_on(cJSON* cmd_args)
{
	uint32_t delay_ms, num_frames;
	
	if (json_parse_stream_on(cmd_args, &delay_ms, &num_frames)) {
		app_set_cmd_record_parameters(delay_ms, num_frames);
		xTaskNotify(task_handle_app, APP_NOTIFY_CMD_START_RECORD_MASK, eSetBits);
		return true;
	}
	
	return false;
}


static bool process_set_time(cJSON* cmd_args)
{
	tmElements_t te;
	
	if (json_parse_set_time(cmd_args, &te)) {
		time_set(te);
		
		// Update the clock in tCam-Mini after we've updated our own time
		lepton_set_time();
		
		return true;
	}
	
	return false;
}


static bool process_set_wifi(cJSON* cmd_args)
{
	char ap_ssid[PS_SSID_MAX_LEN+1];
	char sta_ssid[PS_SSID_MAX_LEN+1];
	char ap_pw[PS_PW_MAX_LEN+1];
	char sta_pw[PS_PW_MAX_LEN+1];
	wifi_info_t new_wifi_info;
	
	new_wifi_info.ap_ssid = ap_ssid;
	new_wifi_info.sta_ssid = sta_ssid;
	new_wifi_info.ap_pw = ap_pw;
	new_wifi_info.sta_pw = sta_pw;
	
	if (json_parse_set_wifi(cmd_args, &new_wifi_info)) {
		// Then update persistent storage
		ps_set_wifi_info(&new_wifi_info);
		xTaskNotify(task_handle_app, APP_NOTIFY_NEW_WIFI_MASK, eSetBits);
		return true;
	}
	
	return false;
}


static bool process_get_fs_list(cJSON* cmd_args)
{
	char dir_name[DIR_NAME_LEN];
	char file_name[FILE_NAME_LEN];  // Unused
	int dir_index;
	
	if (!power_get_sdcard_present()) {
		return false;
	} else if (json_parse_file_cmd_args(cmd_args, &dir_name[0], &file_name[0])) {
		if (strcmp(dir_name, "/") == 0) {
			// Requesting directory list
			dir_index = -1;
		} else {
			// Requesting file list
			dir_index = file_get_named_directory_index(dir_name);
		}
		
		file_set_catalog_index(FILE_REQ_SRC_CMD, dir_index);
		xTaskNotify(task_handle_file, FILE_NOTIFY_CMD_GET_CATALOG_MASK, eSetBits);
		return true;
	}
	
	return false;
}


static bool process_get_fs_file(cJSON* cmd_args)
{
	bool image_is_video;
	char dir_name[DIR_NAME_LEN];
	char file_name[FILE_NAME_LEN];
	
	if (!power_get_sdcard_present()) {
		return false;
	} else if (json_parse_file_cmd_args(cmd_args, &dir_name[0], &file_name[0])) {
		image_is_video = strstr(file_name, ".tmjsn") != NULL;
		file_set_get_image(FILE_REQ_SRC_CMD, dir_name, file_name);
		if (image_is_video) {
			xTaskNotify(task_handle_file, FILE_NOTIFY_CMD_GET_VIDEO_MASK, eSetBits);
		} else {
			xTaskNotify(task_handle_file, FILE_NOTIFY_CMD_GET_IMAGE_MASK, eSetBits);
		}
		return true;
	}
	
	return false;
}


static bool process_del_fs_obj(cJSON* cmd_args)
{
	char dir_name[DIR_NAME_LEN];
	char file_name[FILE_NAME_LEN];
	
	if (!power_get_sdcard_present()) {
		return false;
	} else if (json_parse_file_cmd_args(cmd_args, &dir_name[0], &file_name[0])) {
		if (strlen(file_name) > 0) {
			file_set_del_image(FILE_REQ_SRC_CMD, dir_name, file_name);
			xTaskNotify(task_handle_file, FILE_NOTIFY_CMD_DEL_FILE_MASK, eSetBits);
		} else {
			file_set_del_dir(FILE_REQ_SRC_CMD, dir_name);
			xTaskNotify(task_handle_file, FILE_NOTIFY_CMD_DEL_DIR_MASK, eSetBits);
		}
		
		return true;
	}
	
	return false;
}


static bool process_fw_upd_request(cJSON* cmd_args)
{
	char fw_version[UPD_MAX_VER_LEN];
	uint32_t fw_length;
	
	if (json_parse_fw_upd_request(cmd_args, &fw_length, fw_version)) {		
		// Setup rsp_task for an update
		rsp_set_fw_upd_req_info(fw_length, fw_version);
		
		// Notify rsp_task
		xTaskNotify(task_handle_rsp, RSP_NOTIFY_FW_UPD_REQ_MASK, eSetBits);
		
		return true;
	}
	
	return false;
}


static bool process_fw_segment(cJSON* cmd_args)
{
	uint32_t seg_start;
	uint32_t seg_length;
	
	if (json_parse_fw_segment(cmd_args, &seg_start, &seg_length, fw_upd_segment)) {
		// Setup rsp_task for the segment
		rsp_set_fw_upd_seg_info(seg_start, seg_length);
		
		// Notify rsp_task
		xTaskNotify(task_handle_rsp, RSP_NOTIFY_FW_UPD_SEG_MASK, eSetBits);
		
		return true;
	}
	
	return false;
}


/**
 * Look for c in the rx_circular_buffer and return its location if found, -1 otherwise
 */
static int in_buffer(char c)
{
	int i;
	
	i = rx_circular_pop_index;
	while (i != rx_circular_push_index) {
		if (c == rx_circular_buffer[i]) {
			return i;
		} else {
			if (i++ >= JSON_MAX_CMD_TEXT_LEN) i = 0;
		}
	}
	
	return -1;
}
