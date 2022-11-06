/*
 * Lepton related utilities
 *
 * Contains utility and access functions for the Lepton.
 *
 * Note: I noticed that on occasion, the first time some commands run on the lepton
 * will fail either silently or with an error response code.  The access routines in
 * this module attempt to detect and retry the commands if necessary.
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
#include "lepton_utilities.h"
#include "esp_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "json_utilities.h"
#include "ps_utilities.h"
#include "sys_utilities.h"
#include "time_utilities.h"
#include "system_config.h"



//
// Lepton Utilities constants
//

// Uncomment to debug
//#define DEBUG_CMD



//
// Lepton Utilities variables
//
static const char* TAG = "lepton_utilities";

// Global tCam-mini initial status
tcam_mini_status_t tcam_mini_status;

// Global Lepton state
lep_config_t lep_st;



//
// Lepton Utilities API
//

void lepton_init()
{
	// Clear the command buffer
	xSemaphoreTake(lep_cmd_buffer.mutex, portMAX_DELAY);
	lep_cmd_buffer.pushP = lep_cmd_buffer.bufferP;
	lep_cmd_buffer.popP = lep_cmd_buffer.bufferP;
	lep_cmd_buffer.length = 0;
	xSemaphoreGive(lep_cmd_buffer.mutex);
	
	
	// Set the tCam-Mini's Lepton state from our persistent storage
	ps_get_lep_state(&lep_st);
	sys_cmd_lep_buffer.length = json_set_config(sys_cmd_lep_buffer.bufferP, lep_st.agc_set_enabled, lep_st.emissivity, lep_st.gain_mode, 
		SET_CONFIG_INC_AGC | SET_CONFIG_INC_EMISSIVITY | SET_CONFIG_INC_GAIN);
	if (sys_cmd_lep_buffer.length != 0) {
		lepton_push_cmd(sys_cmd_lep_buffer.bufferP, sys_cmd_lep_buffer.length);
	}
	
	// Set the tCam-Mini clock
	lepton_set_time();

	// Get the tCam-Mini status - so we know what version it is
	tcam_mini_status.model = CAMERA_CAP_MASK_LEP_UNK;
	sys_cmd_lep_buffer.length = json_get_status_cmd(sys_cmd_lep_buffer.bufferP);
	if (sys_cmd_lep_buffer.length != 0) {
		lepton_push_cmd(sys_cmd_lep_buffer.bufferP, sys_cmd_lep_buffer.length);
	}
}


void lepton_set_time()
{
	tmElements_t tm;
	
	time_get(&tm);
	sys_cmd_lep_buffer.length = json_get_set_time(sys_cmd_lep_buffer.bufferP, &tm);
	if (sys_cmd_lep_buffer.length != 0) {
		lepton_push_cmd(sys_cmd_lep_buffer.bufferP, sys_cmd_lep_buffer.length);
	}
}


void lepton_stream_on()
{
	// Load a stream_on command
	sys_cmd_lep_buffer.length = json_get_stream_on_cmd(sys_cmd_lep_buffer.bufferP, 0, 0);
	if (sys_cmd_lep_buffer.length != 0) {
		lepton_push_cmd(sys_cmd_lep_buffer.bufferP, sys_cmd_lep_buffer.length);
	}
}



void lepton_push_cmd(char* buf, uint32_t len)
{
	int i;
	
	// Atomically load lep_cmd_buffer
	xSemaphoreTake(lep_cmd_buffer.mutex, portMAX_DELAY);
	
	// Only load if there's room for this response
	if (len <= (LEP_COMMAND_BUFFER_LEN - lep_cmd_buffer.length)) {
		for (i=0; i<len; i++) {
			// Push data
			*lep_cmd_buffer.pushP = *(buf+i);
			
			// Increment push pointer
			if (++lep_cmd_buffer.pushP >= (lep_cmd_buffer.bufferP + LEP_COMMAND_BUFFER_LEN)) {
				lep_cmd_buffer.pushP = lep_cmd_buffer.bufferP;
			}
		}
		
		lep_cmd_buffer.length += len;
	}
	
	xSemaphoreGive(lep_cmd_buffer.mutex);

#ifdef DEBUG_CMD
	*(buf + len) = 0;
	ESP_LOGI(TAG, "Push: %s", buf);
#endif
}


int lepton_cmd_data_available()
{
	int len;
	
	xSemaphoreTake(lep_cmd_buffer.mutex, portMAX_DELAY);
	len = lep_cmd_buffer.length;
	xSemaphoreGive(lep_cmd_buffer.mutex);
	
	return len;
}


char lepton_pop_cmd_buffer()
{
	char c;
	
	c = *lep_cmd_buffer.popP;
	
	if (++lep_cmd_buffer.popP >= (lep_cmd_buffer.bufferP + CMD_RESPONSE_BUFFER_LEN)) {
		lep_cmd_buffer.popP = lep_cmd_buffer.bufferP;
	}
	
	return c;
}


void lepton_dec_cmd_buffer_len(int len)
{
	xSemaphoreTake(lep_cmd_buffer.mutex, portMAX_DELAY);
	lep_cmd_buffer.length -= len;
	if (lep_cmd_buffer.length < 0) lep_cmd_buffer.length = 0;
	xSemaphoreGive(lep_cmd_buffer.mutex);
}


void lepton_agc(bool en)
{
	// Load a set_config command into lep_tasks's command buffer
	sys_cmd_lep_buffer.length = json_set_config(sys_cmd_lep_buffer.bufferP, en, 0, 0, SET_CONFIG_INC_AGC);
	if (sys_cmd_lep_buffer.length != 0) {
		lepton_push_cmd(sys_cmd_lep_buffer.bufferP, sys_cmd_lep_buffer.length);
	}
}


void lepton_ffc()
{
	// Load a run_ffc command into lep_tasks's command buffer
	sys_cmd_lep_buffer.length = json_get_run_ffc(sys_cmd_lep_buffer.bufferP);
	if (sys_cmd_lep_buffer.length != 0) {
		lepton_push_cmd(sys_cmd_lep_buffer.bufferP, sys_cmd_lep_buffer.length);
	}
}


void lepton_gain_mode(uint8_t mode)
{
	// Load a set_config command into lep_tasks's command buffer
	sys_cmd_lep_buffer.length = json_set_config(sys_cmd_lep_buffer.bufferP, false, 0, (int) mode, SET_CONFIG_INC_GAIN);
	if (sys_cmd_lep_buffer.length != 0) {
		lepton_push_cmd(sys_cmd_lep_buffer.bufferP, sys_cmd_lep_buffer.length);
	}
}


void lepton_spotmeter(uint16_t r1, uint16_t c1, uint16_t r2, uint16_t c2)
{
	// Load a set_spotmeter command into lep_tasks's command buffer
	sys_cmd_lep_buffer.length = json_get_set_spotmeter_cmd(sys_cmd_lep_buffer.bufferP, r1, c1, r2, c2);
	if (sys_cmd_lep_buffer.length != 0) {
		lepton_push_cmd(sys_cmd_lep_buffer.bufferP, sys_cmd_lep_buffer.length);
	}
}


void lepton_emissivity(uint16_t e)
{
	// Load a set_config command into lep_tasks's command buffer
	sys_cmd_lep_buffer.length = json_set_config(sys_cmd_lep_buffer.bufferP, false, (int) e, 0, SET_CONFIG_INC_EMISSIVITY);
	if (sys_cmd_lep_buffer.length != 0) {
		lepton_push_cmd(sys_cmd_lep_buffer.bufferP, sys_cmd_lep_buffer.length);
	}
}


// Call after tcam_mini_status set
int lepton_get_model()
{
	switch (tcam_mini_status.model & CAMERA_CAP_MASK_LEP_MSK) {
		case CAMERA_CAP_MASK_LEP3_0:
			return LEP_TYPE_3_0;
		case CAMERA_CAP_MASK_LEP3_1:
			return LEP_TYPE_3_1;
		case CAMERA_CAP_MASK_LEP3_5:
			return LEP_TYPE_3_5;
		default:
			return LEP_TYPE_UNK;
	}
}


uint32_t lepton_get_tel_status(uint16_t* tel_buf)
{
	return (tel_buf[LEP_TEL_STATUS_HIGH] << 16) | tel_buf[LEP_TEL_STATUS_LOW];
}


/**
 * Convert a temperature reading from the lepton (in units of K * 100) to C
 */
float lepton_kelvin_to_C(uint32_t k, float lep_res)
{
	return (((float) k) * lep_res) - 273.15;
}
