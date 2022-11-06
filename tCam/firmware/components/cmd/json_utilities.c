/*
 * JSON related utilities
 *
 * Contains functions to generate json text objects and parse text objects into the
 * json objects used by firecam.  Uses the cjson library.  Image data is formatted
 * using Base64 encoding.
 *
 * This module uses two pre-allocated buffers for the json text objects.  One for image
 * data (that can be stored as a file or sent to the host) and one for smaller responses
 * to the host.
 *
 * Be sure to read the requirements about freeing allocated buffers or objects in
 * the function description.  Or BOOM.
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
#include "json_utilities.h"
#include "ps_utilities.h"
#include "system_config.h"
#include "file_utilities.h"
#include "gui_utilities.h"
#include "lepton_utilities.h"
#include "power_utilities.h"
#include "time_utilities.h"
#include "upd_utilities.h"
#include "app_task.h"
#include "cmd_task.h"
#include "mbedtls/base64.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include <string.h>



//
// Command parser
//
typedef struct {
	const char* cmd_name;
	int cmd_index;
} cmd_name_t;

const cmd_name_t command_list[CMD_NUM] = {
	{CMD_GET_STATUS_S, CMD_GET_STATUS},
	{CMD_GET_IMAGE_S, CMD_GET_IMAGE},
	{CMD_GET_CONFIG_S, CMD_GET_CONFIG},
	{CMD_SET_CONFIG_S, CMD_SET_CONFIG},
	{CMD_SET_TIME_S, CMD_SET_TIME},
	{CMD_GET_WIFI_S, CMD_GET_WIFI},
	{CMD_SET_WIFI_S, CMD_SET_WIFI},
	{CMD_SET_SPOT_S, CMD_SET_SPOT},
	{CMD_STREAM_ON_S, CMD_STREAM_ON},
	{CMD_STREAM_OFF_S, CMD_STREAM_OFF},
	{CMD_TAKE_PIC_S, CMD_TAKE_PIC},
	{CMD_RECORD_ON_S, CMD_RECORD_ON},
	{CMD_RECORD_OFF_S, CMD_RECORD_OFF},
	{CMD_POWEROFF_S, CMD_POWEROFF},
	{CMD_RUN_FFC_S, CMD_RUN_FFC},
	{CMD_GET_FS_LIST_S, CMD_GET_FS_LIST},
	{CMD_GET_FS_FILE_S, CMD_GET_FS_FILE},
	{CMD_DEL_FS_OBJ_S, CMD_DEL_FS_OBJ},
	{CMD_GET_LEP_CCI_S, CMD_GET_LEP_CCI},
	{CMD_SET_LEP_CCI_S, CMD_SET_LEP_CCI},
	{CMD_FW_UPD_REQ_S, CMD_FW_UPD_REQ},
	{CMD_FW_UPD_SEG_S, CMD_FW_UPD_SEG},
	{CMD_DUMP_SCREEN_S, CMD_DUMP_SCREEN}
};


// Fast base64 decoder array
static const int B64index[256] =
{
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  62, 63, 62, 62, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 0,  0,  0,  0,  0,  0,
    0,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 0,  0,  0,  0,  63,
    0,  26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51
};



//
// JSON Utilities variables
//
static const char* TAG = "json_utilities";

static unsigned char* base64_lep_data;
static unsigned char* base64_lep_telem_data;
static unsigned char* base64_cci_reg_data;

static uint16_t* cci_buf;           // Used to hold Lepton CCI data from cmd or for rsp



//
// JSON Utilities Forward Declarations for internal functions
//
static bool json_add_lep_image_object(cJSON* parent, lep_buffer_t* lep_buffer);
static void json_free_lep_base64_image();
static bool json_add_lep_telem_object(cJSON* parent, lep_buffer_t* lep_buffer);
static void json_free_lep_base64_telem();
static bool json_add_cci_reg_base64_data(cJSON* parent, int len, uint16_t* buf);
static void json_free_cci_reg_base64_data();
static bool json_add_metadata_object(cJSON* parent);
static int json_generate_response_string(cJSON* root, char* json_string);
static bool json_ip_string_to_array(uint8_t* ip_array, char* ip_string);
static int fast_base64_decode(const unsigned char* data, const int len, unsigned char* dst);



//
// JSON Utilities API
//

/**
 * Pre-allocate buffers
 */
bool json_init()
{
	cci_buf = heap_caps_malloc(1024, MALLOC_CAP_SPIRAM);
	if (cci_buf == NULL) {
		ESP_LOGE(TAG, "Could not allocate cci data buffer");
		return false;
	}
	
	return true;
}


/**
 * Create a json command object from a string, returns NULL if it fails.  The object
 * will need to be freed using json_free_object when it is no longer necessary.
 */
cJSON* json_get_object(char* json_string)
{
	cJSON* obj;
	
	obj = cJSON_Parse(json_string);
	
	return obj;
}


/**
 * Free the json command object
 */
void json_free_object(cJSON* obj)
{
	if (obj != NULL) cJSON_Delete(obj);
}


/**
 * Update a formatted json string in a pre-allocated json text image buffer containing
 * three json objects for a lepton image buffer.  Returns a non-zero length for a successful
 * operation.
 *   - Image meta-data
 *   - Base64 encoded raw image from the Lepton
 *   - Base64 encoded telemetry from the Lepton
 *
 * This function handles its own memory management.
 */
uint32_t json_get_image_file_string(char* json_image_text, lep_buffer_t* lep_buffer)
{
	bool success;
	int len = 0;
	cJSON* root;
	
	root = cJSON_CreateObject();
	if (root == NULL) return 0;
	
	// Construct the json object
	success = json_add_metadata_object(root);
	if (success) {
		success = json_add_lep_image_object(root, lep_buffer);
		if (success) {
			success = json_add_lep_telem_object(root, lep_buffer);
			if (!success) {
				// Free lep_image that was already allocated
				json_free_lep_base64_image();
			}
		}
	}
	
	// Print the object to our buffer
	if (success) {
		if (cJSON_PrintPreallocated(root, json_image_text, JSON_MAX_IMAGE_TEXT_LEN, false) == 0) {
			len = 0;
		} else {
			len = strlen(json_image_text);
		}
		
		// Free the base-64 converted image strings
		json_free_lep_base64_image();
		json_free_lep_base64_telem();
	} else {
		ESP_LOGE(TAG, "failed to create json image text");
	}
	
	cJSON_Delete(root);
	
	return len;
}


/**
 * Return a formatted json string containing a get_config command.  Include the delimiters
 * since this string will be sent via the lepton serial interface.
 */
int json_get_config_cmd(char* json_string)
{
	cJSON* root;
	int len;
	
	// Create the command
	root=cJSON_CreateObject();
	if (root == NULL) return 0;
	
	cJSON_AddStringToObject(root, "cmd", "get_config");
	
	// Tightly print the object into the buffer with delimiters
	len = json_generate_response_string(root, json_string);
	
	cJSON_Delete(root);
	
	return len;
}


/**
 * Return a formatted string containing the camera's operating parameters in
 * response to the get_config commmand.  Include the delimiters since this string
 * will be sent via the socket interface.
 */
int json_get_config(char* json_string)
{
	cJSON* root;
	cJSON* config;
	int len = 0;
	lep_config_t* lep_stP;
	
	// Get state
	lep_stP = lep_get_lep_st();
	
	// Create and add to the config object
	root=cJSON_CreateObject();
	if (root == NULL) return 0;
	
	cJSON_AddItemToObject(root, "config", config=cJSON_CreateObject());
	
	cJSON_AddNumberToObject(config, "agc_enabled", lep_stP->agc_set_enabled);
	cJSON_AddNumberToObject(config, "emissivity", lep_stP->emissivity);
	cJSON_AddNumberToObject(config, "gain_mode", lep_stP->gain_mode);
	
	// Tightly print the object into the buffer with delimiters
	len = json_generate_response_string(root, json_string);
	
	cJSON_Delete(root);
	
	return len;
}


/**
 * Return a formatted json string containing a set_config command.  Include the delimiters
 * since this string will be sent via the lepton serial interface.
 */
int json_set_config(char* json_string, bool agc, int emissivity, int gain, int inc_flags)
{
	cJSON* root;
	cJSON* config;
	int len = 0;
	
	if ((inc_flags & (SET_CONFIG_INC_AGC | SET_CONFIG_INC_EMISSIVITY | SET_CONFIG_INC_GAIN)) == 0) return 0;
	
	// Create and add to the config object
	root=cJSON_CreateObject();
	if (root == NULL) return 0;
	
	cJSON_AddStringToObject(root, "cmd", "set_config");
	cJSON_AddItemToObject(root, "args", config=cJSON_CreateObject());
	
	if ((inc_flags & SET_CONFIG_INC_AGC) != 0) {
		cJSON_AddNumberToObject(config, "agc_enabled", agc);
	}
	
	if ((inc_flags & SET_CONFIG_INC_EMISSIVITY) != 0) {
		cJSON_AddNumberToObject(config, "emissivity", emissivity);
	}
	
	if ((inc_flags & SET_CONFIG_INC_GAIN) != 0) {
		cJSON_AddNumberToObject(config, "gain_mode", gain);
	}
	
	// Tightly print the object into the buffer with delimiters
	len = json_generate_response_string(root, json_string);
	
	cJSON_Delete(root);
	
	return len;
}


/**
 * Return a formatted json string containing a get_status command.  Include the delimiters
 * since this string will be sent via the lepton serial interface.
 */
int json_get_status_cmd(char* json_string)
{
	cJSON* root;
	int len = 0;
	
	// Create the command
	root=cJSON_CreateObject();
	if (root == NULL) return 0;
	cJSON_AddStringToObject(root, "cmd", "get_status");
	
	// Tightly print the object into the buffer with delimiters
	len = json_generate_response_string(root, json_string);
	
	cJSON_Delete(root);
	
	return len;
}


/**
 * Return a formatted json string containing the system status in response to the
 * get_status command.  Include the delimiters since this string will be sent via
 * the socket interface.
 */
int json_get_status(char* json_string)
{
	char buf[40];
	cJSON* root;
	cJSON* status;
	int len = 0;
	int model_field;
	gui_state_t* gui_stP;
	wifi_info_t* wifi_infoP;
	const esp_app_desc_t* app_desc;
	tmElements_t te;
	batt_status_t batt;
	int batt_level;
	
	// Get system information
	app_desc = esp_ota_get_app_description();
	gui_stP = gui_get_gui_st();
	time_get(&te);
	power_get_batt(&batt);
	
	// Create and add to the metadata object
	root=cJSON_CreateObject();
	if (root == NULL) return 0;
	
	cJSON_AddItemToObject(root, "status", status=cJSON_CreateObject());
	
	wifi_infoP = wifi_get_info();
	cJSON_AddStringToObject(status, "Camera", wifi_infoP->ap_ssid);
	
	model_field = CAMERA_CAP_MASK_CORE | CAMERA_MODEL_NUM;
	switch (lepton_get_model()) {
		case LEP_TYPE_3_0:
			model_field |= CAMERA_CAP_MASK_LEP3_0;
			break;
		case LEP_TYPE_3_1:
			model_field |= CAMERA_CAP_MASK_LEP3_1;
			break;
		case LEP_TYPE_3_5:
			model_field |= CAMERA_CAP_MASK_LEP3_5;
			break;
		default:
			model_field |= CAMERA_CAP_MASK_LEP_UNK;
	}
	cJSON_AddNumberToObject(status, "Model", model_field);
	
	cJSON_AddStringToObject(status, "Version", app_desc->version);
	
	cJSON_AddNumberToObject(status, "Recording", (const double) gui_stP->recording);
	
	time_get_full_time_string(te, buf);
	cJSON_AddStringToObject(status, "Time", buf);

	time_get_full_date_string(te, buf);
	cJSON_AddStringToObject(status, "Date", buf);
	
	cJSON_AddNumberToObject(status, "Battery", (const double) batt.batt_voltage);
	
	switch (batt.batt_state) {
		case BATT_100:
			batt_level = 100;
			break;
		case BATT_75:
			batt_level = 75;
			break;
		case BATT_50:
			batt_level = 50;
			break;
		case BATT_25:
			batt_level = 25;
			break;
		default:
			batt_level = 0;
	}
	cJSON_AddNumberToObject(status, "Battery Level", batt_level);
	
	switch (batt.charge_state) {
		case CHARGE_OFF:
			strcpy(buf, "OFF");
			break;
		case CHARGE_ON:
			strcpy(buf, "ON");
			break;
		case CHARGE_DONE:
			strcpy(buf, "DONE");
			break;
		case CHARGE_FAULT:
			strcpy(buf, "FAULT");
			break;
	}
	cJSON_AddStringToObject(status, "Charge", buf);
	
	// Tightly print the object into the buffer with delimiters
	len = json_generate_response_string(root, json_string);
	
	cJSON_Delete(root);
	
	return len;
}


/**
 * Return a formatted json string containing the wifi setup (minus password) in response
 * to the get_wifi command.  Include the delimiters since this string will be sent via
 * the socket interface.
 */
int json_get_wifi(char* json_string)
{
	char ip_string[16];  // "XXX:XXX:XXX:XXX" + null
	cJSON* root;
	cJSON* wifi;
	int len = 0;
	wifi_info_t* wifi_infoP;
	
	// Get wifi information
	wifi_infoP = wifi_get_info();
	
	// Create and add to the metadata object
	root=cJSON_CreateObject();
	if (root == NULL) return 0;
	
	cJSON_AddItemToObject(root, "wifi", wifi=cJSON_CreateObject());
	
	cJSON_AddStringToObject(wifi, "ap_ssid", wifi_infoP->ap_ssid);
	cJSON_AddStringToObject(wifi, "sta_ssid", wifi_infoP->sta_ssid);
	cJSON_AddNumberToObject(wifi, "flags", (const double) wifi_infoP->flags);
	
	sprintf(ip_string, "%d.%d.%d.%d", wifi_infoP->ap_ip_addr[3],
			                          wifi_infoP->ap_ip_addr[2],
			                          wifi_infoP->ap_ip_addr[1],
			                          wifi_infoP->ap_ip_addr[0]);
	cJSON_AddStringToObject(wifi, "ap_ip_addr", ip_string);
	
	sprintf(ip_string, "%d.%d.%d.%d", wifi_infoP->sta_ip_addr[3],
			                          wifi_infoP->sta_ip_addr[2],
			                          wifi_infoP->sta_ip_addr[1],
			                          wifi_infoP->sta_ip_addr[0]);
	cJSON_AddStringToObject(wifi, "sta_ip_addr", ip_string);
	
	sprintf(ip_string, "%d.%d.%d.%d", wifi_infoP->sta_netmask[3],
			                          wifi_infoP->sta_netmask[2],
			                          wifi_infoP->sta_netmask[1],
			                          wifi_infoP->sta_netmask[0]);
	cJSON_AddStringToObject(wifi, "sta_netmask", ip_string);
	
	sprintf(ip_string, "%d.%d.%d.%d", wifi_infoP->cur_ip_addr[3],
			                          wifi_infoP->cur_ip_addr[2],
			                          wifi_infoP->cur_ip_addr[1],
			                          wifi_infoP->cur_ip_addr[0]);
	cJSON_AddStringToObject(wifi, "cur_ip_addr", ip_string);
	
	// Tightly print the object into the buffer with delimiters
	len = json_generate_response_string(root, json_string);
	
	cJSON_Delete(root);
	
	return len;
}


/**
 * Return a formatted json string containing the cmd and specified length of base64-encoded
 * CCI register data in response to the get_lep_cci/set_lep_cci command.  Include the delimiters
 * since this string will be sent via the socket interface.
 */
int json_get_cci_response(char* json_string, uint16_t cmd, int cci_len, uint16_t status, uint16_t* buf)
{
	bool success = true;
	int len = 0;
	cJSON* root;
	cJSON* cci_reg;
	
	// Create and add to the cci_reg object
	root = cJSON_CreateObject();
	if (root == NULL) return 0;
	
	cJSON_AddItemToObject(root, "cci_reg", cci_reg=cJSON_CreateObject());
	
	cJSON_AddNumberToObject(cci_reg, "command", cmd);
	cJSON_AddNumberToObject(cci_reg, "length", cci_len);
	cJSON_AddNumberToObject(cci_reg, "status", status);
	
	if (cci_len != 0) {
		success = json_add_cci_reg_base64_data(cci_reg, cci_len, buf);
	}
	
	// Tightly print the object into our buffer with delimiters
	if (success) {
		len = json_generate_response_string(root, json_string);
		if (cci_len != 0) {
			json_free_cci_reg_base64_data();
		}
	}
	cJSON_Delete(root);
	
	return len;
}


/**
 * Generate a formatted json string containing the numeric and string info fields.  Add
 * delimiters for transmission over the network.  Returns string length.
 *
 * Note: Because this function is designed to be used by rsp_task, a valid buffer
 *       must be passed in for json_string.
 */
int json_get_cam_info(char* json_string, uint32_t info_value, char* info_string)
{
	cJSON* root;
	cJSON* response;
	int len = 0;
	
	// Create and add to the metadata object
	root=cJSON_CreateObject();
	if (root != NULL) {	
		// Create and add to the metadata object
		cJSON_AddItemToObject(root, "cam_info", response=cJSON_CreateObject());
		
		cJSON_AddNumberToObject(response, "info_value", info_value);
		cJSON_AddStringToObject(response, "info_string", info_string);
		
		// Tightly print the object into the buffer with delimiters
		len = json_generate_response_string(root, json_string);
		
		cJSON_Delete(root);
	}
	
	return len;
}


/**
 * Generate a formatted json string containing the get_filesystem_list response. Add
 * delimiters for transmission over the network.  Returns string length.
 */
int json_get_filesystem_list_response(char* json_string, char* dir_name, char* name_list)
{
	cJSON* root;
	cJSON* response;
	int len = 0;
	
	// Create and add to the metadata object
	root=cJSON_CreateObject();
	if (root != NULL) {	
		// Create and add to the metadata object
		cJSON_AddItemToObject(root, "filesystem_list", response=cJSON_CreateObject());
		
		cJSON_AddStringToObject(response, "dir_name", dir_name);
		cJSON_AddStringToObject(response, "name_list", name_list);
		
		// Tightly print the object into the buffer with delimiters
		len = json_generate_response_string(root, json_string);
		
		cJSON_Delete(root);
	}
	
	return len;
}


/**
 * Generate a formatted json string containing the "video_info" object.  Add
 * delimiters for transmission over the network.  Returns string length.
 */
int json_get_video_info(char* json_string, tmElements_t start_t, tmElements_t end_t, int n)
{
	char buf[40];
	cJSON* root;
	cJSON* info;
	int len = 0;
	
	// Create and add to the metadata object
	root=cJSON_CreateObject();
	if (root != NULL) {
		// Create and add to the metadata object
		cJSON_AddItemToObject(root, "video_info", info=cJSON_CreateObject());
		
		time_get_full_date_string(end_t, buf);
		cJSON_AddStringToObject(info, "end_date", buf);
		
		time_get_full_time_string(end_t, buf);
		cJSON_AddStringToObject(info, "end_time", buf);
		
		cJSON_AddNumberToObject(info, "num_frames", n);
		
		time_get_full_date_string(start_t, buf);
		cJSON_AddStringToObject(info, "start_date", buf);
		
		time_get_full_time_string(start_t, buf);
		cJSON_AddStringToObject(info, "start_time", buf);
		
		cJSON_AddNumberToObject(info, "version", 1);
		
		// Tightly print the object into the buffer with delimiters
		len = json_generate_response_string(root, json_string);
		
		cJSON_Delete(root);
	}
	
	return len;
}


/**
 * Return a formatted json string containing a run_ffc command.  Include the delimiters
 * since this string will be sent via the lepton serial interface.
 */
int json_get_run_ffc(char* json_string)
{
	cJSON* root;
	int len;
	
	// Create the command
	root=cJSON_CreateObject();
	if (root == NULL) return 0;
	
	cJSON_AddStringToObject(root, "cmd", "run_ffc");
	
	// Tightly print the object into the buffer with delimiters
	len = json_generate_response_string(root, json_string);
	
	cJSON_Delete(root);
	
	return len;
}


/**
 * Return a formatted json string containing a stream_on command.  Include the delimiters
 * since this string will be sent via the lepton serial interface.
 */
int json_get_stream_on_cmd(char* json_string, uint32_t delay_ms, uint32_t* num_frames)
{
	cJSON* root;
	cJSON* args;
	int len;
	
	// Create the command
	root=cJSON_CreateObject();
	if (root == NULL) return 0;
	
	cJSON_AddStringToObject(root, "cmd", "stream_on");
	cJSON_AddItemToObject(root, "args", args=cJSON_CreateObject());
	
	cJSON_AddNumberToObject(args, "delay_msec", (int) delay_ms);
	cJSON_AddNumberToObject(args, "num_frames", (int) num_frames);
	
	// Tightly print the object into the buffer with delimiters
	len = json_generate_response_string(root, json_string);
	
	cJSON_Delete(root);
	
	return len;
}


/**
 * Return a formatted json string containing a set_spotmeter command.  Include the delimiters
 * since this string will be sent via the lepton serial interface.
 */
int json_get_set_spotmeter_cmd(char* json_string, uint16_t r1, uint16_t c1, uint16_t r2, uint16_t c2)
{
	cJSON* root;
	cJSON* args;
	int len;
	
	// Create the command
	root=cJSON_CreateObject();
	if (root == NULL) return 0;
	
	cJSON_AddStringToObject(root, "cmd", "set_spotmeter");
	cJSON_AddItemToObject(root, "args", args=cJSON_CreateObject());
	
	cJSON_AddNumberToObject(args, "c1", c1);
	cJSON_AddNumberToObject(args, "c2", c2);
	cJSON_AddNumberToObject(args, "r1", r1);
	cJSON_AddNumberToObject(args, "r2", r2);
	
	// Tightly print the object into the buffer with delimiters
	len = json_generate_response_string(root, json_string);
	
	cJSON_Delete(root);
	
	return len;
}


/**
 * Return a formatted json string containing a set_time command.  Include the delimiters
 * since this string will be sent via the lepton serial interface.
 */
int json_get_set_time(char* json_string, tmElements_t* te)
{
	cJSON* root;
	cJSON* args;
	int len;
	
	// Create the command
	root=cJSON_CreateObject();
	if (root == NULL) return 0;
	
	cJSON_AddStringToObject(root, "cmd", "set_time");
	cJSON_AddItemToObject(root, "args", args=cJSON_CreateObject());
	
	cJSON_AddNumberToObject(args, "sec", te->Second);
	cJSON_AddNumberToObject(args, "min", te->Minute);
	cJSON_AddNumberToObject(args, "hour", te->Hour);
	cJSON_AddNumberToObject(args, "dow", te->Wday);
	cJSON_AddNumberToObject(args, "day", te->Day);
	cJSON_AddNumberToObject(args, "mon", te->Month);
	cJSON_AddNumberToObject(args, "year", te->Year);
	
	// Tightly print the object into the buffer with delimiters
	len = json_generate_response_string(root, json_string);
	
	cJSON_Delete(root);
	
	return len;
}


/**
 * Return a formatted json string containing a get_fw command.  Include the delimiters since
 * this string will be sent over the network.  Returns string length.
 */
int json_get_get_fw(char* json_string, uint32_t fw_start, uint32_t fw_len)
{
	cJSON* root;
	cJSON* get_fw;
	int len;
	
	// Create and add to the cci_reg object
	root = cJSON_CreateObject();
	if (root == NULL) return 0;
	
	cJSON_AddItemToObject(root, "get_fw", get_fw=cJSON_CreateObject());
	
	cJSON_AddNumberToObject(get_fw, "start", fw_start);
	cJSON_AddNumberToObject(get_fw, "length", fw_len);
	
	// Tightly print the object into our buffer with delimitors
	len = json_generate_response_string(root, json_string);
	
	cJSON_Delete(root);
	
	return len;
}


/**
 * Return a formatted json string containing a dump_screen_response.  Indexes buf with start_loc
 * for len uint8_t bytes.  Include the delimiters since this string will be sent over the network.
 * Returns string length.
 */
int json_get_dump_screen_response(char* json_string, int start_loc, int num_bytes, uint8_t* buf)
{
	bool success = true;
	int len = 0;
	cJSON* root;
	cJSON* data;
	
	// Create and add to the cci_reg object
	root = cJSON_CreateObject();
	if (root == NULL) return 0;
	
	cJSON_AddItemToObject(root, "screen_dump_response", data=cJSON_CreateObject());
	
	cJSON_AddNumberToObject(data, "start", start_loc);
	cJSON_AddNumberToObject(data, "length", num_bytes);
	
	// Add "data" using existing routine for CCI data since it mallocs in the SPI RAM
	// and can handle large data arrays
	if (num_bytes != 0) {
		success = json_add_cci_reg_base64_data(data, num_bytes/2, (uint16_t*) &buf[start_loc]);
	}
	
	// Tightly print the object into our buffer with delimiters
	if (success) {
		json_string[0] = CMD_JSON_STRING_START;
		if (cJSON_PrintPreallocated(root, &json_string[1], JSON_MAX_IMAGE_TEXT_LEN, false) == 0) {
			len = 0;
		} else {
			len = strlen(json_string);
			json_string[len] = CMD_JSON_STRING_STOP;
			json_string[len+1] = 0;
			len += 1;
		}
		
		if (num_bytes != 0) {
			json_free_cci_reg_base64_data();
		}
	}
	cJSON_Delete(root);
	
	return len;
}


/**
 * Parse a top level command object, returning the command number and a pointer to 
 * a json object containing "args".  The pointer is set to NULL if there are no args.
 */
bool json_parse_cmd(cJSON* cmd_obj, int* cmd, cJSON** cmd_args)
{
	 cJSON *cmd_type = cJSON_GetObjectItem(cmd_obj, "cmd");
	 char* cmd_name;
	 int i;
	 
	 if (cmd_type != NULL) {
	 	cmd_name = cJSON_GetStringValue(cmd_type);

	 	if (cmd_name != NULL) {
	 		*cmd = CMD_UNKNOWN;
	 		
	 		for (i=0; i<CMD_NUM; i++) {
	 			if (strcmp(cmd_name, command_list[i].cmd_name) == 0) {
	 				*cmd = command_list[i].cmd_index;
	 				break;
	 			}
	 		}
	 		
	 		*cmd_args = cJSON_GetObjectItem(cmd_obj, "args");
	 		
	 		return true;
	 	}
	 }

	 return false;
}


/**
 * Fill in a lep_config_t struct with arguments from a set_config command, preserving
 * unmodified elements
 */
bool json_parse_set_config(cJSON* cmd_args, lep_config_t* new_st)
{
	int item_count = 0;
	lep_config_t* lep_stP;
	
	// Get existing settings to be possibly overwritten by the command
	lep_stP = lep_get_lep_st();
	new_st->agc_set_enabled = lep_stP->agc_set_enabled;
	new_st->emissivity = lep_stP->emissivity;
	new_st->gain_mode = lep_stP->gain_mode;
	
	if (cmd_args != NULL) {
		if (cJSON_HasObjectItem(cmd_args, "agc_enabled")) {
			new_st->agc_set_enabled = cJSON_GetObjectItem(cmd_args, "agc_enabled")->valueint > 0 ? true : false;
			item_count++;
		}
		
		if (cJSON_HasObjectItem(cmd_args, "emissivity")) {
			new_st->emissivity = cJSON_GetObjectItem(cmd_args, "emissivity")->valueint;
			if (new_st->emissivity < 1) new_st->emissivity = 1;
			if (new_st->emissivity > 100) new_st->emissivity = 100;
			item_count++;
		}
		
		if (cJSON_HasObjectItem(cmd_args, "gain_mode")) {
			new_st->gain_mode = cJSON_GetObjectItem(cmd_args, "gain_mode")->valueint;
			if (new_st->gain_mode > LEP_GAIN_AUTO) new_st->gain_mode = LEP_GAIN_AUTO;
			item_count++;
		}
		
		return (item_count > 0);
	}
	
	return false;
}


/**
 * Get spotmeter coordinates
 */
bool json_parse_set_spotmeter(cJSON* cmd_args, uint16_t* r1, uint16_t* c1, uint16_t* r2, uint16_t* c2)
{
	int item_count = 0;
	int i;
	
	if (cmd_args != NULL) {
		if (cJSON_HasObjectItem(cmd_args, "r1")) {
			i = cJSON_GetObjectItem(cmd_args, "r1")->valueint;
			if (i < 0) i = 0;
			if (i > (LEP_HEIGHT-2)) i = LEP_HEIGHT - 2;
			*r1 = i;
			item_count++;
		}
		
		if (cJSON_HasObjectItem(cmd_args, "c1")) {
			i = cJSON_GetObjectItem(cmd_args, "c1")->valueint;
			if (i < 0) i = 0;
			if (i > (LEP_WIDTH-2)) i = LEP_WIDTH - 2;
			*c1 = i;
			item_count++;
		}
		
		if (cJSON_HasObjectItem(cmd_args, "r2")) {
			i = cJSON_GetObjectItem(cmd_args, "r2")->valueint;
			if (i < (*r1+1)) i = *r1 + 1;
			if (i > (LEP_HEIGHT-1)) i = LEP_HEIGHT - 1;
			*r2 = i;
			item_count++;
		}
		
		if (cJSON_HasObjectItem(cmd_args, "c2")) {
			i = cJSON_GetObjectItem(cmd_args, "c2")->valueint;
			if (i < (*c1+1)) i = *c1 + 1;
			if (i > (LEP_WIDTH-1)) i = LEP_WIDTH - 1;
			*c2 = i;
			item_count++;
		}
		
		return (item_count == 4);
	}
	
	return false;
}


/**
 * Fill in a tmElements object with arguments from a set_time command
 */
bool json_parse_set_time(cJSON* cmd_args, tmElements_t* te)
{
	int item_count = 0;
	
	if (cmd_args != NULL) {
		if (cJSON_HasObjectItem(cmd_args, "sec")) {
			te->Second = cJSON_GetObjectItem(cmd_args, "sec")->valueint; // 0 - 59
			item_count++;
		}
		
		if (cJSON_HasObjectItem(cmd_args, "min")) {
			te->Minute = cJSON_GetObjectItem(cmd_args, "min")->valueint; // 0 - 59
			item_count++;
		}
		
		if (cJSON_HasObjectItem(cmd_args, "hour")) {
			te->Hour   = cJSON_GetObjectItem(cmd_args, "hour")->valueint; // 0 - 23
			item_count++;
		}
		
		if (cJSON_HasObjectItem(cmd_args, "dow")) {
			te->Wday   = cJSON_GetObjectItem(cmd_args, "dow")->valueint; // 1 - 7
			item_count++;
		}
		
		if (cJSON_HasObjectItem(cmd_args, "day")) {
			te->Day    = cJSON_GetObjectItem(cmd_args, "day")->valueint; // 1 - 31
			item_count++;
		}
		
		if (cJSON_HasObjectItem(cmd_args, "mon")) {
			te->Month  = cJSON_GetObjectItem(cmd_args, "mon")->valueint; // 1 - 12
			item_count++;
		}
		
		if (cJSON_HasObjectItem(cmd_args, "year")) {
			te->Year   = cJSON_GetObjectItem(cmd_args, "year")->valueint; // offset from 1970
			item_count++;
		}
		
		return (item_count == 7);
	}
	
	return false;
}


/**
 * Fill in a wifi_info_t object with arguments from a set_wifi command, preserving
 * unmodified elements
 */
bool json_parse_set_wifi(cJSON* cmd_args, wifi_info_t* new_wifi_info)
{
	char* s;
	int i;
	int item_count = 0;
	wifi_info_t* wifi_infoP;
	
	// Get existing settings
	wifi_infoP = wifi_get_info();
	
	if (cmd_args != NULL) {
		if (cJSON_HasObjectItem(cmd_args, "ap_ssid")) {
			s = cJSON_GetObjectItem(cmd_args, "ap_ssid")->valuestring;
			i = strlen(s);
			if (i == 0) {
				ESP_LOGE(TAG, "set_wifi zero length ap_ssid");
				return false;
			} else if (i <= PS_SSID_MAX_LEN) {
				strcpy(new_wifi_info->ap_ssid, s);
				item_count++;
			} else {
				ESP_LOGE(TAG, "set_wifi ap_ssid: %s too long", s);
				return false;
			}
		} else {
			strcpy(new_wifi_info->ap_ssid, wifi_infoP->ap_ssid);
		}
		
		if (cJSON_HasObjectItem(cmd_args, "sta_ssid")) {
			s = cJSON_GetObjectItem(cmd_args, "sta_ssid")->valuestring;
			i = strlen(s);
			if (i == 0) {
				ESP_LOGE(TAG, "set_wifi zero length sta_ssid");
				return false;
			} else if (i <= PS_SSID_MAX_LEN) {
				strcpy(new_wifi_info->sta_ssid, s);
				item_count++;
			} else {
				ESP_LOGE(TAG, "set_wifi sta_ssid: %s too long", s);
				return false;
			}
		} else {
			strcpy(new_wifi_info->sta_ssid, wifi_infoP->sta_ssid);
		}
		
		if (cJSON_HasObjectItem(cmd_args, "ap_pw")) {
			s = cJSON_GetObjectItem(cmd_args, "ap_pw")->valuestring;
			i = strlen(s);
			if ((i >= 8) && (i <= PS_PW_MAX_LEN)) {
				strcpy(new_wifi_info->ap_pw, s);
				item_count++;
			} else {
				ESP_LOGE(TAG, "set_wifi ap_pw: %s must be between 8 and %d characters", s, PS_PW_MAX_LEN);
				return false;
			}
		} else {
			strcpy(new_wifi_info->ap_pw, wifi_infoP->ap_pw);
		}
		
		if (cJSON_HasObjectItem(cmd_args, "sta_pw")) {
			s = cJSON_GetObjectItem(cmd_args, "sta_pw")->valuestring;
			i = strlen(s);
			if ((i >= 8) && (i <= PS_PW_MAX_LEN)) {
				strcpy(new_wifi_info->sta_pw, s);
				item_count++;
			} else {
				ESP_LOGE(TAG, "set_wifi sta_pw: %s must be between 8 and %d characters", s, PS_PW_MAX_LEN);
				return false;
			}
		} else {
			strcpy(new_wifi_info->sta_pw, wifi_infoP->sta_pw);
		}
		
		if (cJSON_HasObjectItem(cmd_args, "flags")) {
			new_wifi_info->flags = (uint8_t) cJSON_GetObjectItem(cmd_args, "flags")->valueint;
			item_count++;
		} else {
			new_wifi_info->flags = wifi_infoP->flags;
		}
		
		if (cJSON_HasObjectItem(cmd_args, "ap_ip_addr")) {
			s = cJSON_GetObjectItem(cmd_args, "ap_ip_addr")->valuestring;
			if (json_ip_string_to_array(new_wifi_info->ap_ip_addr, s)) {
				item_count++;
			} else {
				ESP_LOGE(TAG, "Illegal set_wifi ap_ip_addr: %s", s);
				return false;
			}
		} else {
			for (i=0; i<4; i++) new_wifi_info->ap_ip_addr[i] = wifi_infoP->ap_ip_addr[i];
		}
		
		if (cJSON_HasObjectItem(cmd_args, "sta_ip_addr")) {
			s = cJSON_GetObjectItem(cmd_args, "sta_ip_addr")->valuestring;
			if (json_ip_string_to_array(new_wifi_info->sta_ip_addr, s)) {
				item_count++;
			} else {
				ESP_LOGE(TAG, "Illegal set_wifi sta_ip_addr: %s", s);
				return false;
			}
		} else {
			for (i=0; i<4; i++) new_wifi_info->sta_ip_addr[i] = wifi_infoP->sta_ip_addr[i];
		}
		
		if (cJSON_HasObjectItem(cmd_args, "sta_netmask")) {
			s = cJSON_GetObjectItem(cmd_args, "sta_netmask")->valuestring;
			if (json_ip_string_to_array(new_wifi_info->sta_netmask, s)) {
				item_count++;
			} else {
				ESP_LOGE(TAG, "Illegal set_wifi sta_netmask: %s", s);
				return false;
			}
		} else {
			for (i=0; i<4; i++) new_wifi_info->sta_netmask[i] = wifi_infoP->sta_netmask[i];
		}
		
		// Just copy existing address over
		for (i=0; i<4; i++) new_wifi_info->cur_ip_addr[i] = wifi_infoP->cur_ip_addr[i];
		
		return (item_count > 0);
	}
	
	return false;
}


/**
 * Get the stream_on arguments
 */
bool json_parse_stream_on(cJSON* cmd_args, uint32_t* delay_ms, uint32_t* num_frames)
{
	int i;
	
	if (cmd_args != NULL) {
		if (cJSON_HasObjectItem(cmd_args, "delay_msec")) {
			i = cJSON_GetObjectItem(cmd_args, "delay_msec")->valueint;
			if (i < 0) i = 0;
			*delay_ms = i;
		} else {
			*delay_ms = 0;
		}
		
		if (cJSON_HasObjectItem(cmd_args, "num_frames")) {
			i = cJSON_GetObjectItem(cmd_args, "num_frames")->valueint;
			if (i < 0) i = 0;
			*num_frames = i;
		} else {
			*num_frames = 0;
		}
	} else {
		// Assume old-style command and setup fastest possible streaming
		*delay_ms = 0;
		*num_frames = 0;
	}
	
	return true;
}


/**
 * Get the arguments for a file command: Directory name and [optionally] File name
 */
bool json_parse_file_cmd_args(cJSON* cmd_args, char* dir_name_buf, char* file_name_buf)
{
	char* s;
	int item_count = 0;
	
	if (cmd_args != NULL) {
		if (cJSON_HasObjectItem(cmd_args, "dir_name")) {
			s = cJSON_GetObjectItem(cmd_args, "dir_name")->valuestring;
			if (strlen(s) <= DIR_NAME_LEN) {
				strcpy(dir_name_buf, s);
				item_count++;
			} else {
				ESP_LOGE(TAG, "file command dir_name: %s is too long", s);
				return false;
			}
		}
		
		if (cJSON_HasObjectItem(cmd_args, "file_name")) {
			s = cJSON_GetObjectItem(cmd_args, "file_name")->valuestring;
			if (strlen(s) <= FILE_NAME_LEN) {
				strcpy(file_name_buf, s);
				item_count++;
			} else {
				ESP_LOGE(TAG, "file command file_name: %s is too long", s);
				return false;
			}
		}
		
		return (item_count > 0);
	}
	
	return false;
}


/**
 * Get the get_lep_cci arguments.  Pass our cci_buf back to the calling code to hold
 * the read data.
 */
bool json_parse_get_lep_cci(cJSON* cmd_args, uint16_t* cmd, int* len, uint16_t** buf)
{
	int i;
	int item_count = 0;
	
	if (cmd_args != NULL) {
		if (cJSON_HasObjectItem(cmd_args, "command")) {
			i = cJSON_GetObjectItem(cmd_args, "command")->valueint;
			*cmd = (uint16_t) i;
			item_count++;
		}
		
		if (cJSON_HasObjectItem(cmd_args, "length")) {
			i = cJSON_GetObjectItem(cmd_args, "length")->valueint;
			*len = i;
			item_count++;
		}
		
		*buf = cci_buf;
		
		return(item_count == 2);
	}
	
	return false;
}


/**
 * Get the set_lep_cci arguments.  Fill our cci_buf with register data and pass it back
 * to the calling code.
 */
bool json_parse_set_lep_cci(cJSON* cmd_args, uint16_t* cmd, int* len, uint16_t** buf)
{
	char* data;
	int i;
	int item_count = 0;
	size_t dec_len;
	
	if (cmd_args != NULL) {
		if (cJSON_HasObjectItem(cmd_args, "command")) {
			i = cJSON_GetObjectItem(cmd_args, "command")->valueint;
			*cmd = (uint16_t) i;
			item_count++;
		}
		
		if (cJSON_HasObjectItem(cmd_args, "length")) {
			i = cJSON_GetObjectItem(cmd_args, "length")->valueint;
			*len = i;
			item_count++;
		}
		
		if (item_count == 2) {
			if (cJSON_HasObjectItem(cmd_args, "data")) {
				data = cJSON_GetObjectItem(cmd_args, "data")->valuestring;
				
				// Decode
				i = mbedtls_base64_decode((unsigned char*) cci_buf, *len*2, &dec_len, (const unsigned char*) data, strlen(data));
				if (i != 0) {
					ESP_LOGE(TAG, "Base 64 CCI Register data decode failed - %d (%d bytes decoded)", i, dec_len);
					return false;
				}
			}
		}
		
		*buf = cci_buf;
		
		return(item_count == 2);
	}
	
	return false;
}


bool json_parse_fw_upd_request(cJSON* cmd_args, uint32_t* len, char* ver)
{
	char* v;
	int i;
	int item_count = 0;
	
	if (cmd_args != NULL) {
		if (cJSON_HasObjectItem(cmd_args, "length")) {
			i = cJSON_GetObjectItem(cmd_args, "length")->valueint;
			*len = (uint32_t) i;
			item_count++;
		}
		
		if (cJSON_HasObjectItem(cmd_args, "version")) {
			v = cJSON_GetObjectItem(cmd_args, "version")->valuestring;
			strncpy(ver, v, UPD_MAX_VER_LEN);
			item_count++;
		}
		
		return(item_count == 2);
	}
	
	return false;
}


bool json_parse_fw_segment(cJSON* cmd_args, uint32_t* start, uint32_t* len, uint8_t* buf)
{
	char* data;
	int i;
	int item_count = 0;
	size_t dec_len;
	size_t enc_len;
	
	if (cmd_args != NULL) {
		if (cJSON_HasObjectItem(cmd_args, "start")) {
			i = cJSON_GetObjectItem(cmd_args, "start")->valueint;
			*start = (uint32_t) i;
			item_count++;
		}
		
		if (cJSON_HasObjectItem(cmd_args, "length")) {
			i = cJSON_GetObjectItem(cmd_args, "length")->valueint;
			*len = (uint32_t) i;
			item_count++;
		}
		
		if (item_count == 2) {
			if (cJSON_HasObjectItem(cmd_args, "data")) {
				data = cJSON_GetObjectItem(cmd_args, "data")->valuestring;
				
				// Decode
				enc_len = strlen(data);
				i = mbedtls_base64_decode(buf, FW_UPD_CHUNK_MAX_LEN, &dec_len, (const unsigned char*) data, enc_len);
				if (i != 0) {
					ESP_LOGE(TAG, "Base 64 FW segment data decode failed - %d (%d bytes decoded)", i, dec_len);
					return false;
				}
				
				item_count++;
			}
		}
		
		return(item_count == 3);
	}
	
	return false;
}


/**
 * Get the image_ready length.
 */
bool json_parse_image_ready(cJSON* obj, uint32_t* len)
{
	if (obj != NULL) {
		if (cJSON_HasObjectItem(obj, "image_ready")) {
			*len = (uint32_t) cJSON_GetObjectItem(obj, "image_ready")->valueint;
			return true;
		} else {
			return false;
		}
	}
	
	return false;
}


/**
 * Fill in a tcam_mini_status_t struct with arguments from a status response
 */
void json_parse_status_rsp(cJSON* status_args, tcam_mini_status_t* status_st)
{
	char* c;
	
	if (status_args != NULL) {
		memset(status_st->camera, 0, sizeof(status_st->camera));
		if (cJSON_HasObjectItem(status_args, "Camera")) {
			c = cJSON_GetObjectItem(status_args, "Camera")->valuestring;
			strncpy(status_st->camera, c, sizeof(status_st->camera) - 1);
		}
		
		if (cJSON_HasObjectItem(status_args, "Model")) {
			status_st->model = (uint32_t) cJSON_GetObjectItem(status_args, "Model")->valueint;
		}
		
		memset(status_st->version, 0, sizeof(status_st->version));
		if (cJSON_HasObjectItem(status_args, "Version")) {
			c = cJSON_GetObjectItem(status_args, "Version")->valuestring;
			strncpy(status_st->version, c, sizeof(status_st->version) - 1);
		}
		
		memset(status_st->time, 0, sizeof(status_st->time));
		if (cJSON_HasObjectItem(status_args, "Time")) {
			c = cJSON_GetObjectItem(status_args, "Time")->valuestring;
			strncpy(status_st->time, c, sizeof(status_st->time) - 1);
		}
		
		memset(status_st->date, 0, sizeof(status_st->date));
		if (cJSON_HasObjectItem(status_args, "Date")) {
			c = cJSON_GetObjectItem(status_args, "Date")->valuestring;
			strncpy(status_st->date, c, sizeof(status_st->date) - 1);
		}
	}
}


/**
 * Fill in a lep_config_t struct with arguments from a config response, preserving
 * unmodified elements
 */
void json_parse_config_rsp(cJSON* rsp_args, lep_config_t* lep_st)
{
	if (rsp_args != NULL) {
		if (cJSON_HasObjectItem(rsp_args, "agc_enabled")) {
			lep_st->agc_set_enabled = cJSON_GetObjectItem(rsp_args, "agc_enabled")->valueint > 0 ? true : false;
		}
		
		if (cJSON_HasObjectItem(rsp_args, "emissivity")) {
			lep_st->emissivity = cJSON_GetObjectItem(rsp_args, "emissivity")->valueint;
			if (lep_st->emissivity < 1) lep_st->emissivity = 1;
			if (lep_st->emissivity > 100) lep_st->emissivity = 100;
		}
		
		if (cJSON_HasObjectItem(rsp_args, "gain_mode")) {
			lep_st->gain_mode = cJSON_GetObjectItem(rsp_args, "gain_mode")->valueint;
			if (lep_st->gain_mode > LEP_GAIN_AUTO) lep_st->gain_mode = LEP_GAIN_AUTO;
		}
	}
}


/**
 * Return a pointer to the name for a known cmd
 */
const char* json_get_cmd_name(int cmd)
{
	int i;
	
	for (i=0; i<CMD_NUM; i++) {
		if (command_list[i].cmd_index == cmd) {
			return command_list[i].cmd_name;
		}
	}
	
	return "Unknown";
}


/**
 * Determine the type of json object read from a file
 */
int json_get_file_object_type(cJSON* obj)
{
	if (obj == NULL) {
		return FILE_JSON_NULL;
	} else if (cJSON_HasObjectItem(obj, "radiometric")) {
		return FILE_JSON_IMAGE;
	} else if (cJSON_HasObjectItem(obj, "video_info")) {
		return FILE_JSON_VIDEO_INFO;
	}
	
	return FILE_JSON_UNKNOWN;
}



/**
 * Parse an image object, converting the data in the metadata to a msec timestamp
 * and filling in the passed-in lepton buffer structure (if lep_img is not NULL).
 */
bool json_parse_image(cJSON* img_obj, uint64_t* ts_msec, lep_buffer_t* lep_img)
{
	bool success = true;
	char* time = NULL;
	char* date = NULL;
	char* data;
	cJSON* meta;
	size_t len = 0;
	int res;
	tmElements_t te;
	uint16_t* lepP;
	
	// Process the metadata, if it exists, to get the timestamp
	if (cJSON_HasObjectItem(img_obj, "metadata")) {
		meta = cJSON_GetObjectItem(img_obj, "metadata");
		
		if (cJSON_HasObjectItem(meta, "Time")) {
			time = cJSON_GetObjectItem(meta, "Time")->valuestring;
		}
		if (cJSON_HasObjectItem(meta, "Date")) {
			date = cJSON_GetObjectItem(meta, "Date")->valuestring;
		}
		if ((time != NULL) && (date != NULL)) {
			time_get_time_from_strings(&te, time, date);
			*ts_msec = time_get_millis(te);
		}
	} else {
		return false;
	}
	
	if (lep_img != NULL) {
		// Process the lepton radiometric data, if it exists
		if (cJSON_HasObjectItem(img_obj, "radiometric")) {
			data = cJSON_GetObjectItem(img_obj, "radiometric")->valuestring;
			
			// Decode
			res = fast_base64_decode((const unsigned char*) data, strlen(data), (unsigned char*) lep_img->lep_bufferP);
//			res = mbedtls_base64_decode((unsigned char*) lep_img->lep_bufferP, LEP_NUM_PIXELS*2, &len, (const unsigned char*) data, strlen(data));
			if (res != 0) {
				ESP_LOGE(TAG, "Obj base 64 radiometric decode failed - %d (%d bytes decoded)", res, len);
				success = false;
			} else {
				// Compute the min/max values
				lep_img->lep_min_val = 0xFFFF;
				lep_img->lep_max_val = 0;
				lepP = lep_img->lep_bufferP + LEP_NUM_PIXELS;
				while (lepP-- != lep_img->lep_bufferP) {
					if (*lepP < lep_img->lep_min_val) lep_img->lep_min_val = *lepP;
					if (*lepP > lep_img->lep_max_val) lep_img->lep_max_val = *lepP;
				}
			}
		} else {
			success = false;
		}		
		
		// Process the lepton telemetry data, if it exists
		if (cJSON_HasObjectItem(img_obj, "telemetry")) {
			data = cJSON_GetObjectItem(img_obj, "telemetry")->valuestring;
			
			// Decode
			res = fast_base64_decode((const unsigned char*) data, strlen(data), (unsigned char*) lep_img->lep_telemP);
//			res = mbedtls_base64_decode((unsigned char*) lep_img->lep_telemP, LEP_TEL_WORDS*2, &len, (const unsigned char*) data, strlen(data));
			if (res != 0) {
				ESP_LOGE(TAG, "Obj base 64 telemetry decode failed - %d (%d bytes decoded)", res, len);
				lep_img->telem_valid = false;
				success = false;
			} else {
				lep_img->telem_valid = true;
			}
		} else {
			success = false;
		}
	}
	
	return success;
}


/**
 * Directly parse an image object string, converting the base64 encoded image and
 * telemetry data to fill in the passed-in lepton buffer structure.
 */
bool json_parse_image_string(char* img, lep_buffer_t* lep_img)
{
	char* imgP;
	char* telP;
	int res;
	size_t len = 0;
	uint16_t* lepP;
	
	// Find the start if the encoded image string
	//   1. Find radiometric/telemetry
	//   2. Skip past the end quote on radiometric/telemetry
	//   3. Find the starting quote of the encoded image string
	//   4. Encoded image string is one position beyond that
	if ((imgP = strstr(img, "radio")) == NULL) {
		res = 0;
		goto json_parse_image_string_err;
	}
	if ((imgP = strchr(imgP+12, '"')) == NULL) {
		res = 1;
		goto json_parse_image_string_err;
	}
	imgP++;
	
	// Find the start of the encoded telemetry string
	if ((telP = strstr(img, "telem")) == NULL) {
		res = 2;
		goto json_parse_image_string_err;
	}
	if ((telP = strchr(telP+10, '"')) == NULL) {
		res = 3;
		goto json_parse_image_string_err;
	}
	telP++;
	
	// Decode the image
	res = fast_base64_decode((const unsigned char*) imgP, LEP_NUM_PIXELS*2*4/3, (unsigned char*) lep_img->lep_bufferP);
//	res = mbedtls_base64_decode((unsigned char*) lep_img->lep_bufferP, LEP_NUM_PIXELS*2, &len, (const unsigned char*) imgP, LEP_NUM_PIXELS*2*4/3);
	if (res != 0) {
		ESP_LOGE(TAG, "String base 64 radiometric decode failed - %d (%d bytes decoded)", res, len);
		return false;
	} else {
		// Compute the min/max values
		lep_img->lep_min_val = 0xFFFF;
		lep_img->lep_max_val = 0;
		lepP = lep_img->lep_bufferP + LEP_NUM_PIXELS;
		while (lepP-- != lep_img->lep_bufferP) {
			if (*lepP < lep_img->lep_min_val) lep_img->lep_min_val = *lepP;
			if (*lepP > lep_img->lep_max_val) lep_img->lep_max_val = *lepP;
		}
	}
	
	// Decode the telemetry
	res = fast_base64_decode((const unsigned char*) telP, LEP_TEL_WORDS*2*4/3, (unsigned char*) lep_img->lep_telemP);
//	res = mbedtls_base64_decode((unsigned char*) lep_img->lep_telemP, LEP_TEL_WORDS*2, &len, (const unsigned char*) telP, LEP_TEL_WORDS*2*4/3);
	if (res != 0) {
		ESP_LOGE(TAG, "String base 64 telemetry decode failed - %d (%d bytes decoded)", res, len);
		return false;
	}
	lep_img->telem_valid = true;
	
	return true;
	
json_parse_image_string_err:
	ESP_LOGE(TAG, "Could not find keyword in image string (%d)", res);
	return false;
}


/**
 * Parse a video_info object, returning information about the video file
 */
bool json_parse_video_info(cJSON* obj, uint64_t* start_msec, uint64_t* end_msec, int* num_frames)
{
	bool success = true;
	char* time = NULL;
	char* date = NULL;
	cJSON* vid_info;
	tmElements_t te;
	
	// Process the video_info, if it exists
	if (cJSON_HasObjectItem(obj, "video_info")) {
		vid_info = cJSON_GetObjectItem(obj, "video_info");
		
		// Get start timestamp
		if (cJSON_HasObjectItem(vid_info, "start_time")) {
			time = cJSON_GetObjectItem(vid_info, "start_time")->valuestring;
		}
		
		if (cJSON_HasObjectItem(vid_info, "start_date")) {
			date = cJSON_GetObjectItem(vid_info, "start_date")->valuestring;
		}
		
		if ((time != NULL) && (date != NULL)) {
			time_get_time_from_strings(&te, time, date);
			*start_msec = time_get_millis(te);
		} else {
			success = false;
		}
		
		// Get end timestamp
		if (cJSON_HasObjectItem(vid_info, "end_time")) {
			time = cJSON_GetObjectItem(vid_info, "end_time")->valuestring;
		}
		
		if (cJSON_HasObjectItem(vid_info, "end_date")) {
			date = cJSON_GetObjectItem(vid_info, "end_date")->valuestring;
		}
			
		if ((time != NULL) && (date != NULL)) {
			time_get_time_from_strings(&te, time, date);
			*end_msec = time_get_millis(te);
		} else {
			success = false;
		}
		
		// Get the number of frames
		if (cJSON_HasObjectItem(vid_info, "num_frames")) {
			*num_frames = cJSON_GetObjectItem(vid_info, "num_frames")->valueint;
		} else {
			success = false;
		}
	} else {
		success = false;
	}
	
	return success;
}



//
// JSON Utilities internal functions
//

/**
 * Add a child object containing base64 encoded lepton image from the shared buffer
 *
 * Note: The encoded image string is held in an array that must be freed with
 * json_free_lep_base64_image() after the json object is converted to a string.
 */
static bool json_add_lep_image_object(cJSON* parent, lep_buffer_t* lep_buffer)
{
	size_t base64_obj_len;
	
	// Get the necessary length and allocate a buffer
	(void) mbedtls_base64_encode(base64_lep_data, 0, &base64_obj_len, 
								 (const unsigned char *) lep_buffer->lep_bufferP, LEP_NUM_PIXELS*2);
	base64_lep_data = heap_caps_malloc(base64_obj_len, MALLOC_CAP_SPIRAM);
	
	if (base64_lep_data != NULL) {
		// Base-64 encode the camera data
		if (mbedtls_base64_encode(base64_lep_data, base64_obj_len, &base64_obj_len, 
							      (const unsigned char *) lep_buffer->lep_bufferP,
	    	                       LEP_NUM_PIXELS*2) != 0) {
	                           
			ESP_LOGE(TAG, "failed to encode lepton image base64 text");
			free(base64_lep_data);
			return false;
		}
	} else {
		ESP_LOGE(TAG, "failed to allocate %d bytes for lepton image base64 text", base64_obj_len);
		return false;
	}
	
	// Add the encoded data as a reference since we're managing the buffer
	cJSON_AddItemToObject(parent, "radiometric", cJSON_CreateStringReference((char*) base64_lep_data));
	
	return true;
}


/**
 * Free the base64-encoded Lepton image.  Call this routine after printing the 
 * image json object.
 */
static void json_free_lep_base64_image()
{
	free(base64_lep_data);
}


/**
 * Add a child object containing base64 encoded lepton telemetry array from the shared buffer
 *
 * Note: The encoded telemetry string is held in an array that must be freed with
 * json_free_lep_base64_telem() after the json object is converted to a string.
 */
static bool json_add_lep_telem_object(cJSON* parent, lep_buffer_t* lep_buffer)
{
	size_t base64_obj_len;
	
	// Get the necessary length and allocate a buffer
	(void) mbedtls_base64_encode(base64_lep_telem_data, 0, &base64_obj_len, 
								 (const unsigned char *) lep_buffer->lep_telemP, LEP_TEL_WORDS*2);
	base64_lep_telem_data = heap_caps_malloc(base64_obj_len, MALLOC_CAP_SPIRAM);
	
	
	if (base64_lep_data != NULL) {
		// Base-64 encode the telemetry array
		if (mbedtls_base64_encode(base64_lep_telem_data, base64_obj_len, &base64_obj_len, 
							      (const unsigned char *) lep_buffer->lep_telemP,
	    	                       LEP_TEL_WORDS*2) != 0) {
	                           
			ESP_LOGE(TAG, "failed to encode lepton telemetry base64 text");
			free(base64_lep_telem_data);
			return false;
		}
	} else {
		ESP_LOGE(TAG, "failed to allocate %d bytes for lepton telemetry base64 text", base64_obj_len);
		return false;
	}
	
	// Add the encoded data as a reference since we're managing the buffer
	cJSON_AddItemToObject(parent, "telemetry", cJSON_CreateStringReference((char*) base64_lep_telem_data));
	
	return true;
}


/**
 * Free the base64-encoded Lepton telemetry string.  Call this routine after printing the 
 * telemetry json object.
 */
static void json_free_lep_base64_telem()
{
	free(base64_lep_telem_data);
}


/**
 * Add a child object containing base64 encoded CCI Register data from buf.
 *
 * Note: The encoded data is held in an array that must be freed with
 * json_free_cci_reg_base64_data() after the json object is converted to a string.
 */
static bool json_add_cci_reg_base64_data(cJSON* parent, int len, uint16_t* buf)
{
	size_t base64_obj_len;
	
	// Get the necessary length and allocate a buffer
	(void) mbedtls_base64_encode(base64_cci_reg_data, 0, &base64_obj_len, 
								 (const unsigned char *) buf, len*2);
	base64_cci_reg_data = heap_caps_malloc(base64_obj_len, MALLOC_CAP_SPIRAM);
	
	
	if (base64_cci_reg_data != NULL) {
		// Base-64 encode the CCI data
		if (mbedtls_base64_encode(base64_cci_reg_data, base64_obj_len, &base64_obj_len, 
							      (const unsigned char *) buf, len*2) != 0) {
	                           
			ESP_LOGE(TAG, "failed to encode CCI Register data base64 text");
			free(base64_cci_reg_data);
			return false;
		}
	} else {
		ESP_LOGE(TAG, "failed to allocate %d bytes for CCI Register base64 text", base64_obj_len);
		return false;
	}
	
	// Add the encoded data as a reference since we're managing the buffer
	cJSON_AddItemToObject(parent, "data", cJSON_CreateStringReference((char*) base64_cci_reg_data));
	
	return true;
}


/**
 * Free the base64-encoded CCI Register data string.  Call this routine after printing the
 * cci_reg json object.
 */
static void json_free_cci_reg_base64_data()
{
	free(base64_cci_reg_data);
}


/**
 * Add a child object containing image metadata to the parent.
 */
static bool json_add_metadata_object(cJSON* parent)
{
	char buf[40];
	int model_field;
	cJSON* meta;
	gui_state_t* gui_stP;
	wifi_info_t* wifi_info;
	const esp_app_desc_t* app_desc;
	tmElements_t te;
	
	// Get system information
	app_desc = esp_ota_get_app_description();
	gui_stP = gui_get_gui_st();
	time_get(&te);
	
	// Create and add to the metadata object
	cJSON_AddItemToObject(parent, "metadata", meta=cJSON_CreateObject());
	
	wifi_info = wifi_get_info();
	cJSON_AddStringToObject(meta, "Camera", wifi_info->ap_ssid);
	
	model_field = CAMERA_CAP_MASK_CORE | CAMERA_MODEL_NUM;
	switch (lepton_get_model()) {
		case LEP_TYPE_3_0:
			model_field |= CAMERA_CAP_MASK_LEP3_0;
			break;
		case LEP_TYPE_3_1:
			model_field |= CAMERA_CAP_MASK_LEP3_1;
			break;
		case LEP_TYPE_3_5:
			model_field |= CAMERA_CAP_MASK_LEP3_5;
			break;
		default:
			model_field |= CAMERA_CAP_MASK_LEP_UNK;
	}
	cJSON_AddNumberToObject(meta, "Model", model_field);
	
	cJSON_AddStringToObject(meta, "Version", app_desc->version);
	
	time_get_full_time_string(te, buf);
	cJSON_AddStringToObject(meta, "Time", buf);

	time_get_full_date_string(te, buf);
	cJSON_AddStringToObject(meta, "Date", buf);
	
	return true;
}


/**
 * Tightly print a response into a string with delimiters for transmission over the network.
 * Returns length of the string.
 */
static int json_generate_response_string(cJSON* root, char* json_string)
{
	int len;
	
	json_string[0] = CMD_JSON_STRING_START;
	if (cJSON_PrintPreallocated(root, &json_string[1], JSON_MAX_RSP_TEXT_LEN, false) == 0) {
		len = 0;
	} else {
		len = strlen(json_string);
		json_string[len] = CMD_JSON_STRING_STOP;
		json_string[len+1] = 0;
		len += 1;
	}
	
	return len;
}


/**
 * Convert a string in the form of "XXX.XXX.XXX.XXX" into a 4-byte array for wifi_info_t
 */
static bool json_ip_string_to_array(uint8_t* ip_array, char* ip_string)
{
	char c;
	int i = 3;
	
	ip_array[i] = 0;
	while ((c = *ip_string++) != 0) {
		if (c == '.') {
			if (i == 0) {
				// Too many '.' characters
				return false;
			} else {
				// Setup for next byte
				ip_array[--i] = 0;
			}
		} else if ((c >= '0') && (c <= '9')) {
			// Add next numeric digit
			ip_array[i] = (ip_array[i] * 10) + (c - '0');
		} else {
			// Illegal character in string
			return false;
		}
	}
	
	return true;
}


/*
 * Fast base64 decoder written by "polfosol" taken from 
 * https://stackoverflow.com/questions/180947/base64-decode-snippet-in-c/13935718
 */
static int fast_base64_decode(const unsigned char* data, const int len, unsigned char* dst)
{
	unsigned char* p = (unsigned char*) data;
	int j = 0;
	int pad1 = len % 4 || p[len - 1] == '=';
	int pad2 = pad1 && (len % 4 > 2 || p[len - 2] != '=');
	int last = (len - pad1) / 4 << 2;
	int i, n;
	
	if (len == 0) return -1;
	
	for (i=0; i<last; i+=4) {
		n = B64index[p[i]] << 18 | B64index[p[i + 1]] << 12 | B64index[p[i + 2]] << 6 | B64index[p[i + 3]];
		dst[j++] = n >> 16;
        dst[j++] = n >> 8 & 0xFF;
        dst[j++] = n & 0xFF;
	}
	
	if (pad1) {
		n = B64index[p[last]] << 18 | B64index[p[last + 1]] << 12;
        dst[j++] = n >> 16;
        if (pad2)
        {
            n |= B64index[p[last + 2]] << 6;
            dst[j++] = n >> 8 & 0xFF;
        }
	}
	
	return 0;
}
