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
#ifndef JSON_UTILITIES_H
#define JSON_UTILITIES_H

#include "rtc.h"
#include "lepton_utilities.h"
#include "sys_utilities.h"
#include "wifi_utilities.h"
#include <stdbool.h>
#include <stdint.h>
#include "cJSON.h"


//
// JSON Utilities Constants
//

// File json objects
#define FILE_JSON_NULL                    0
#define FILE_JSON_IMAGE                   1
#define FILE_JSON_VIDEO_INFO              2
#define FILE_JSON_UNKNOWN                 3

// set_config inc_flags
#define SET_CONFIG_INC_AGC                0x01
#define SET_CONFIG_INC_EMISSIVITY         0x02
#define SET_CONFIG_INC_GAIN               0x03



//
// JSON Utilities API
//
bool json_init();

cJSON* json_get_object(char* json_string);
void json_free_object(cJSON* obj);

uint32_t json_get_image_file_string(char* json_image_text, lep_buffer_t* lep_buffer);
int json_get_config_cmd(char* json_string);
int json_get_config(char* json_string);
int json_set_config(char* json_string, bool agc, int emissivity, int gain, int inc_flags);
int json_get_status_cmd(char* json_string);
int json_get_status(char* json_string);
int json_get_wifi(char* json_string);
int json_get_cci_response(char* json_string, uint16_t cmd, int cci_len, uint16_t status, uint16_t* buf);
int json_get_cam_info(char* json_string, uint32_t info_value, char* info_string);
int json_get_filesystem_list_response(char* json_string, char* dir_name, char* name_list);
int json_get_video_info(char* json_string, tmElements_t start_t, tmElements_t end_t, int n);
int json_get_run_ffc(char* json_string);
int json_get_stream_on_cmd(char* json_string, uint32_t delay_ms, uint32_t* num_frames);
int json_get_set_spotmeter_cmd(char* json_string, uint16_t r1, uint16_t c1, uint16_t r2, uint16_t c2);
int json_get_set_time(char* json_string, tmElements_t* te);
int json_get_get_fw(char* json_string, uint32_t fw_start, uint32_t fw_len);
int json_get_dump_screen_response(char* json_string, int start_loc, int num_bytes, uint8_t* buf);

bool json_parse_cmd(cJSON* cmd_obj, int* cmd, cJSON** cmd_args);
bool json_parse_set_config(cJSON* cmd_args, lep_config_t* new_st);
bool json_parse_set_spotmeter(cJSON* cmd_args, uint16_t* r1, uint16_t* c1, uint16_t* r2, uint16_t* c2);
bool json_parse_set_time(cJSON* cmd_args, tmElements_t* te);
bool json_parse_set_wifi(cJSON* cmd_args, wifi_info_t* new_wifi_info);
bool json_parse_stream_on(cJSON* cmd_args, uint32_t* delay_ms, uint32_t* num_frames);
bool json_parse_file_cmd_args(cJSON* cmd_args, char* dir_name_buf, char* file_name_buf);
bool json_parse_get_lep_cci(cJSON* cmd_args, uint16_t* cmd, int* len, uint16_t** buf);
bool json_parse_set_lep_cci(cJSON* cmd_args, uint16_t* cmd, int* len, uint16_t** buf);
bool json_parse_fw_upd_request(cJSON* cmd_args, uint32_t* len, char* ver);
bool json_parse_fw_segment(cJSON* cmd_args, uint32_t* start, uint32_t* len, uint8_t* buf);
bool json_parse_image_ready(cJSON* obj, uint32_t* len);
void json_parse_status_rsp(cJSON* status_args, tcam_mini_status_t* status_st);
void json_parse_config_rsp(cJSON* rsp_args, lep_config_t* lep_st);
const char* json_get_cmd_name(int cmd);

int json_get_file_object_type(cJSON* obj);
bool json_parse_image(cJSON* img_obj, uint64_t* ts_msec, lep_buffer_t* lep_img);
bool json_parse_image_string(char* img, lep_buffer_t* lep_img);
bool json_parse_video_info(cJSON* obj, uint64_t* start_msec, uint64_t* end_msec, int* num_frames);

#endif /* JSON_UTILITIES_H */