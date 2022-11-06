/*
 * Persistent Storage Module
 *
 * Manage the persistent storage kept in the gCore EFM8 RAM and provide access
 * routines to it.
 *
 * NOTE: It is assumed that only one task will access persistent storage at a time.
 * This is done to eliminate the need for mutex protection, that could cause a 
 * dead-lock with another process also accessing a device via I2C.
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
#include "ps_utilities.h"
#include "system_config.h"
#include "esp_system.h"
#include "esp_log.h"
#include "gcore.h"
#include "palettes.h"
#include <stdbool.h>
#include <string.h>


//
// PS Utilities internal constants
//

// "Magic Word" constants
#define PS_MAGIC_WORD_0 0x34
#define PS_MAGIC_WORD_1 0x12

// Layout version - to allow future firmware versions to change the layout without
// losing data
#define PS_LAYOUT_VERSION 1 

// Static Memory Array indicies
#define PS_MAGIC_WORD_0_ADDR   0
#define PS_MAGIC_WORD_1_ADDR   1
#define PS_LAYOUT_VERSION_ADDR 2
#define PS_FIRST_DATA_ADDR     3
#define PS_CHECKSUM_ADDR       (PS_RAM_SIZE - 1)

// Maximum bytes available for storage
#define PS_MAX_DATA_BYTES      (PS_RAM_SIZE - 4)

// Number of separate data parameter sub-regions
#define PS_REGION_GUI          0
#define PS_REGION_LEP          1
#define PS_REGION_WIFI         2
#define PS_NUM_REGIONS         3

// GUI State boolean flags 
#define PS_GUI_INTERP_EN_MASK  0x01
#define PS_GUI_MAN_RANGE_MASK  0x02
#define PS_GUI_METRIC_MASK     0x04
#define PS_GUI_SPOT_EN_MASK    0x08

// Lepton state boolean flags
#define PS_LEP_AGC_EN_MASK     0x01

// Stored Wifi Flags bitmask
#define PS_WIFI_FLAG_MASK      (WIFI_INFO_FLAG_STARTUP_ENABLE | WIFI_INFO_FLAG_CL_STATIC_IP | WIFI_INFO_FLAG_CLIENT_MODE)



//
// PS Utilities enums and structs
//

enum ps_update_types_t {
	FULL,                      // Update all bytes in the external SRAM
	GUI,                       // Update GUI state related and checksum
	LEP,                       // Update Lepton state related and checksum
	WIFI                       // Update wifi-related and checksum
};


//
// Persistent Storage data structures - data stored in battery backed RAM
// Note: Careful attention must be paid to make sure these are packed structures
//

// PS pointers into the shadow buffer for parameter sub-regions
typedef struct {
	uint8_t start_index[PS_NUM_REGIONS];
	uint8_t length[PS_NUM_REGIONS];
} ps_sub_region_t;


// Stored GUI parameters
typedef struct {
	uint8_t flags;
	uint8_t palette;
	uint8_t lcd_brightness;
	uint32_t man_range_min;
	uint32_t man_range_max;
	uint32_t recording_interval;
} ps_gui_state_t;


// Stored Lepton Parameters
typedef struct {
	uint8_t flags;
	uint8_t emissivity;
	uint8_t gain_mode;
} ps_lep_config_t;


// Stored Wifi Parameters (strings are null-terminated)
typedef struct {
	char ap_ssid[PS_SSID_MAX_LEN+1];
	char sta_ssid[PS_SSID_MAX_LEN+1];
	char ap_pw[PS_PW_MAX_LEN+1];
	char sta_pw[PS_PW_MAX_LEN+1];
	uint8_t flags;
	uint8_t ap_ip_addr[4];
	uint8_t sta_ip_addr[4];
	uint8_t sta_netmask[4];
} ps_wifi_info_t;



//
// PS Utilities Internal variables
//
static const char* TAG = "ps_utilities";

// Our local copy for reading
static uint8_t ps_shadow_buffer[PS_RAM_SIZE];

// Copy read at boot to check for changes (and flash update)
static uint8_t ps_check_buffer[PS_RAM_SIZE];

// Indexes of and lengths of the parameter sub-regions
static ps_sub_region_t ps_sub_regions;



//
// PS Utilities Forward Declarations for internal functions
//
static bool ps_read_array();
static bool ps_write_array(enum ps_update_types_t t);
static void ps_init_array();
static void ps_store_string(char* dst, char* src, uint8_t max_len);
static bool ps_valid_magic_word();
static uint8_t ps_compute_checksum();
static bool ps_write_bytes_to_gcore(uint16_t start_addr, uint16_t data_len);
static char ps_nibble_to_ascii(uint8_t n);



//
// PS Utilities API
//

/**
 * Initialize persistent storage
 *   - Load our local buffer
 *   - Initialize it and the NVRAM with valid data if necessary
 */
bool ps_init()
{
	int n;
	
	// Setup our access data structure
	ps_sub_regions.start_index[PS_REGION_GUI] = PS_FIRST_DATA_ADDR;
	ps_sub_regions.length[PS_REGION_GUI] = sizeof(ps_gui_state_t);
	n = sizeof(ps_gui_state_t);
	ps_sub_regions.start_index[PS_REGION_LEP] = ps_sub_regions.start_index[PS_REGION_GUI] +
	                                            ps_sub_regions.length[PS_REGION_GUI];
	ps_sub_regions.length[PS_REGION_LEP] = sizeof(ps_lep_config_t);
	n += sizeof(ps_lep_config_t);
	ps_sub_regions.start_index[PS_REGION_WIFI] = ps_sub_regions.start_index[PS_REGION_LEP] +
	                                            ps_sub_regions.length[PS_REGION_LEP];
	ps_sub_regions.length[PS_REGION_WIFI] = sizeof(ps_wifi_info_t);
	n += sizeof(ps_wifi_info_t);
	if (n > PS_MAX_DATA_BYTES) {
		// This should never occur - mainly for debugging
		ESP_LOGE(TAG, "NVRAM does not have enough room for %d bytes\n", n);
		return false;
	} else {
		ESP_LOGI(TAG, "Using %d of %d bytes", n, PS_MAX_DATA_BYTES);
	}
	
	// Get the persistent data from the battery-backed PMIC/RTC chip
	if (!ps_read_array()) {
		ESP_LOGE(TAG, "Failed to read persistent data from NVRAM");
	}
	
	// Make a copy to check when powering down
	for (n=0; n<PS_RAM_SIZE; n++) {
		ps_check_buffer[n] = ps_shadow_buffer[n];
	}
	
	// Check if it is initialized with valid data, initialize if not
	if (!ps_valid_magic_word() || (ps_compute_checksum() != ps_shadow_buffer[PS_CHECKSUM_ADDR])) {
		ESP_LOGI(TAG, "Initialize persistent storage with default values");
		ps_init_array();
		if (!ps_write_array(FULL)) {
			ESP_LOGE(TAG, "Failed to write persistent data to NVRAM");
			return false;
		}
	}
	
	return true;
}


/**
 * Reset persistent storage to factory default values.  Store these in both the
 * battery-backed PMIC/RTC chip and backing flash (if necessary).
 */
void ps_set_factory_default()
{
	// Re-initialize persistent data and write it to battery-backed RAM
	ESP_LOGI(TAG, "Re-initialize persistent storage with default values");
	ps_init_array();
	if (!ps_write_array(FULL)) {
		ESP_LOGE(TAG, "Failed to write persistent data to NVRAM");
	}
	
	// Save to the PMIC/RTC flash memory (will execute only if changes are detected)
	ps_save_to_flash();
}


/**
 * Write battery-backed RAM to flash in the PMIC/RTC if any changes are detected.
 * We perform the dirty check to avoid unnecessary flash writes.
 */
void ps_save_to_flash()
{
	bool dirty_flag = false;
	int i;
	uint8_t reg = 1;
	
	// Check for any changed data since we booted indicating the need to save
	// NVRAM to flash
	for (i=0; i<PS_RAM_SIZE; i++) {
		if (ps_check_buffer[i] != ps_shadow_buffer[i]) {
			dirty_flag = true;
			break;
		}
	}
	
	if (dirty_flag) {
		ESP_LOGI(TAG, "Saving NVRAM");
		
		// Trigger a write of the NVRAM to backing flash
		if (gcore_set_reg8(GCORE_REG_NV_CTRL, GCORE_NVRAM_WR_TRIG)) {
			// Wait after triggering the write
			//   1. 36 mSec to allow the EFM8 to erase the flash memory (it is essentially
			//      locked up while doing this and won't respond to I2C cycles).
			//   2. 128 mSec to allow the NVRAM to be written to flash since.  This isn't
			//      strictly necessary before starting to poll for completion but since 
			//      it's possible an I2C cycle will fail during the writing process we
			//      wait so we don't freak anyone out who might be looking at the log
			//      output.
			vTaskDelay(pdMS_TO_TICKS(155));
			
			// Poll until write is done - this should fall through immediately
			while (reg != 0) {
				vTaskDelay(pdMS_TO_TICKS(10));
				(void) gcore_get_reg8(GCORE_REG_NV_CTRL, &reg);
			}
		}
	} 
}


void ps_get_gui_state(gui_state_t* state)
{
	ps_gui_state_t* psP;
	
	// Map the ps gui structure to the shadow buffer for easy access
	psP = (ps_gui_state_t*) &ps_shadow_buffer[ps_sub_regions.start_index[PS_REGION_GUI]];
	
	state->display_interp_enable = (psP->flags & PS_GUI_INTERP_EN_MASK) != 0;
	state->spotmeter_enable = (psP->flags & PS_GUI_SPOT_EN_MASK) != 0;
	state->temp_unit_C = (psP->flags & PS_GUI_METRIC_MASK) != 0;
	state->man_range_mode = (psP->flags & PS_GUI_MAN_RANGE_MASK) != 0;
	
	state->palette = (int) psP->palette;
	state->lcd_brightness = (int) psP->lcd_brightness;
	state->man_range_min = (int) psP->man_range_min;
	state->man_range_max = (int) psP->man_range_max;
	state->recording_interval = (int) psP->recording_interval;
}


void ps_set_gui_state(const gui_state_t* state)
{
	ps_gui_state_t* psP;
	
	// Map the ps gui structure to the shadow buffer for easy access
	psP = (ps_gui_state_t*) &ps_shadow_buffer[ps_sub_regions.start_index[PS_REGION_GUI]];
	
	psP->flags = (state->display_interp_enable ? PS_GUI_INTERP_EN_MASK : 0) |
	             (state->spotmeter_enable ? PS_GUI_SPOT_EN_MASK : 0) |
	             (state->temp_unit_C ? PS_GUI_METRIC_MASK : 0) |
	             (state->man_range_mode ? PS_GUI_MAN_RANGE_MASK : 0);
	             
	psP->palette = (uint8_t) state->palette;
	psP->lcd_brightness = (uint8_t) state->lcd_brightness;
	psP->man_range_min = (uint32_t) state->man_range_min;
	psP->man_range_max = (uint32_t) state->man_range_max;
	psP->recording_interval = (uint32_t) state->recording_interval;
	
	ps_shadow_buffer[PS_CHECKSUM_ADDR] = ps_compute_checksum();
	if (!ps_write_array(GUI)) {
		ESP_LOGE(TAG, "Failed to write GUI state to NVRAM");
	}
}


void ps_get_lep_state(lep_config_t* state)
{
	ps_lep_config_t* psP;
	
	// Map the ps lep structure to the shadow buffer for easy access
	psP = (ps_lep_config_t*) &ps_shadow_buffer[ps_sub_regions.start_index[PS_REGION_LEP]];
	
	state->agc_set_enabled = (psP->flags & PS_LEP_AGC_EN_MASK) != 0;
	
	state->emissivity = (int) psP->emissivity;
	state->gain_mode = (int) psP->gain_mode;
}


void ps_set_lep_state(const lep_config_t* state)
{
	ps_lep_config_t* psP;
	
	// Map the ps lep structure to the shadow buffer for easy access
	psP = (ps_lep_config_t*) &ps_shadow_buffer[ps_sub_regions.start_index[PS_REGION_LEP]];
	
	psP->flags = (state->agc_set_enabled ? PS_LEP_AGC_EN_MASK : 0);
	
	psP->emissivity = (uint8_t) state->emissivity;
	psP->gain_mode = (uint8_t) state->gain_mode;
	
	ps_shadow_buffer[PS_CHECKSUM_ADDR] = ps_compute_checksum();
	if (!ps_write_array(LEP)) {
		ESP_LOGE(TAG, "Failed to write LEP state to NVRAM");
	}
}


void ps_get_wifi_info(wifi_info_t* info)
{
	int i;
	ps_wifi_info_t* psP;
	
	// Map the ps wifi structure to the shadow buffer for easy access
	psP = (ps_wifi_info_t*) &ps_shadow_buffer[ps_sub_regions.start_index[PS_REGION_WIFI]];
	
	strcpy(info->ap_ssid, (const char*) psP->ap_ssid);
	strcpy(info->ap_pw, (const char*) psP->ap_pw);
	strcpy(info->sta_ssid, (const char*) psP->sta_ssid);
	strcpy(info->sta_pw, (const char*) psP->sta_pw);
	
	info->flags = psP->flags & PS_WIFI_FLAG_MASK;
	
	for (i=0; i<4; i++) {
		info->ap_ip_addr[i] = psP->ap_ip_addr[i];
		info->sta_ip_addr[i] = psP->sta_ip_addr[i];
		info->sta_netmask[i] = psP->sta_netmask[i];
	}
}


void ps_set_wifi_info(const wifi_info_t* info)
{
	int i;
	ps_wifi_info_t* psP;
	
	// Map the ps wifi structure to the shadow buffer for easy access
	psP = (ps_wifi_info_t*) &ps_shadow_buffer[ps_sub_regions.start_index[PS_REGION_WIFI]];
	
	ps_store_string(psP->ap_ssid, info->ap_ssid, PS_SSID_MAX_LEN);
	ps_store_string(psP->ap_pw, info->ap_pw, PS_PW_MAX_LEN);
	ps_store_string(psP->sta_ssid, info->sta_ssid, PS_SSID_MAX_LEN);
	ps_store_string(psP->sta_pw, info->sta_pw, PS_PW_MAX_LEN);
	
	psP->flags = info->flags & PS_WIFI_FLAG_MASK;
	
	for (i=0; i<4; i++) {
		 psP->ap_ip_addr[i] = info->ap_ip_addr[i];
		 psP->sta_ip_addr[i] = info->sta_ip_addr[i];
		 psP->sta_netmask[i] = info->sta_netmask[i];
	}
	
	ps_shadow_buffer[PS_CHECKSUM_ADDR] = ps_compute_checksum();
	if (!ps_write_array(WIFI)) {
		ESP_LOGE(TAG, "Failed to write WiFi data to NVRAM");
	}
}


bool ps_has_new_cam_name(const wifi_info_t* info)
{
	ps_wifi_info_t* psP;
	
	// Map the ps wifi structure to the shadow buffer for easy access
	psP = (ps_wifi_info_t*) &ps_shadow_buffer[ps_sub_regions.start_index[PS_REGION_WIFI]];
	
	return(strncmp(psP->ap_ssid, info->ap_ssid, PS_SSID_MAX_LEN) != 0);
}



//
// PS Utilities internal functions
//

/**
 * Load our local buffer from the NVRAM
 */
static bool ps_read_array()
{
	return (gcore_get_nvram_bytes(PS_RAM_STARTADDR, ps_shadow_buffer, PS_RAM_SIZE) == true);
}


/**
 * Write parts (to reduce locked I2C time) or the full local buffer to NVRAM
 */
static bool ps_write_array(enum ps_update_types_t t)
{
	bool ret = false;
	
	switch(t) {
	case FULL:
		ret = ps_write_bytes_to_gcore(0, PS_RAM_SIZE);
		break;

	case GUI:
		if (ps_write_bytes_to_gcore(ps_sub_regions.start_index[PS_REGION_GUI],
		                          ps_sub_regions.length[PS_REGION_GUI]))
		{
			ret = gcore_set_nvram_byte(PS_RAM_STARTADDR + PS_CHECKSUM_ADDR,
			                       ps_shadow_buffer[PS_CHECKSUM_ADDR]);
		} else {
			ret = false;
		}
		break;
	
	case LEP:
		if (ps_write_bytes_to_gcore(ps_sub_regions.start_index[PS_REGION_LEP],
		                          ps_sub_regions.length[PS_REGION_LEP]))
		{
			ret = gcore_set_nvram_byte(PS_RAM_STARTADDR + PS_CHECKSUM_ADDR,
			                       ps_shadow_buffer[PS_CHECKSUM_ADDR]);
		} else {
			ret = false;
		}
		break;	
		
	case WIFI:
		if (ps_write_bytes_to_gcore(ps_sub_regions.start_index[PS_REGION_WIFI],
		                          ps_sub_regions.length[PS_REGION_WIFI]))
		{
			ret = gcore_set_nvram_byte(PS_RAM_STARTADDR + PS_CHECKSUM_ADDR,
			                       ps_shadow_buffer[PS_CHECKSUM_ADDR]);
		} else {
			ret = false;
		}
		break;
	}
	return ret;
}


/**
 * Initialize our local array with default values.
 */
static void ps_init_array()
{
	uint8_t sys_mac_addr[6];
	ps_gui_state_t* gP;
	ps_lep_config_t* lP;
	ps_wifi_info_t* wP;
	
	// Associate parameter sub-regions with the shadow buffer
	gP = (ps_gui_state_t*) &ps_shadow_buffer[ps_sub_regions.start_index[PS_REGION_GUI]];
	lP = (ps_lep_config_t*) &ps_shadow_buffer[ps_sub_regions.start_index[PS_REGION_LEP]];
	wP = (ps_wifi_info_t*) &ps_shadow_buffer[ps_sub_regions.start_index[PS_REGION_WIFI]];
		
	// Zero buffer
	memset(ps_shadow_buffer, 0, PS_RAM_SIZE);
	
	// Control fields
	ps_shadow_buffer[PS_MAGIC_WORD_0_ADDR] = PS_MAGIC_WORD_0;
	ps_shadow_buffer[PS_MAGIC_WORD_1_ADDR] = PS_MAGIC_WORD_1;
	ps_shadow_buffer[PS_LAYOUT_VERSION_ADDR] = PS_LAYOUT_VERSION;
	
	// GUI parameters
	gP->flags = PS_GUI_INTERP_EN_MASK | PS_GUI_SPOT_EN_MASK;
	gP->palette = PALETTE_FUSION;
	gP->lcd_brightness = 50;
	gP->man_range_min = 27315;  // 0C - 0F
	gP->man_range_max = 37315;  // 100C - 212F
	gP->recording_interval = 0; // Fastest
	
	// Lepton parameters
	lP->flags = 0;              // Radiometric mode
	lP->emissivity = 100;
	lP->gain_mode = LEP_GAIN_HIGH;
	
	// Wifi parameters
	//
	// Get the system's default MAC address and add 1 to match the "Soft AP" mode
	// (see "Miscellaneous System APIs" in the ESP-IDF documentation)
	esp_efuse_mac_get_default(sys_mac_addr);
	sys_mac_addr[5] = sys_mac_addr[5] + 1;
	
	// Construct our default AP SSID/Camera name
	sprintf(wP->ap_ssid, "%s%c%c%c%c", PS_DEFAULT_AP_SSID,
		    ps_nibble_to_ascii(sys_mac_addr[4] >> 4),
		    ps_nibble_to_ascii(sys_mac_addr[4]),
		    ps_nibble_to_ascii(sys_mac_addr[5] >> 4),
	 	    ps_nibble_to_ascii(sys_mac_addr[5]));
	// Leave sta_ssid, ap_pw, sta_pw as null strings (from zero buffer) since we default to AP
	
	wP->flags = WIFI_INFO_FLAG_STARTUP_ENABLE;
	
	wP->ap_ip_addr[3] = 192;
	wP->ap_ip_addr[2] = 168;
	wP->ap_ip_addr[1] = 4;
	wP->ap_ip_addr[0] = 1;
	wP->sta_ip_addr[3] = 192;
	wP->sta_ip_addr[2] = 168;
	wP->sta_ip_addr[1] = 4;
	wP->sta_ip_addr[0] = 2;
	wP->sta_netmask[3] = 255;
	wP->sta_netmask[2] = 255;
	wP->sta_netmask[1] = 255;
	wP->sta_netmask[0] = 0;
	
	// Finally compute and load checksum
	ps_shadow_buffer[PS_CHECKSUM_ADDR] = ps_compute_checksum();
}


/**
 * Store a string at the specified location in our local buffer making sure it does
 * not exceed the available space and is terminated with a null character.
 */
static void ps_store_string(char* dst, char* src, uint8_t max_len)
{
	char c;
	int i = 0;
	bool saw_s_end = false;
	
	while (i < max_len) {
		if (!saw_s_end) {
			// Copy string data
			c = *(src+i);
			*(dst+i) = c;
			if (c == 0) saw_s_end = true;
		} else {
			// Pad with nulls
			*(dst+i) = 0;
		}
		i++;
	}
	
	// One final null in case the string was max_len long
	*(dst+i) = 0;
}


/**
 * Return true if our local array starts with the magic word
 */
static bool ps_valid_magic_word()
{
	return ((ps_shadow_buffer[PS_MAGIC_WORD_0_ADDR] == PS_MAGIC_WORD_0) &&
	        (ps_shadow_buffer[PS_MAGIC_WORD_1_ADDR] == PS_MAGIC_WORD_1));
}


/**
 * Compute the checksum over all non-checksum bytes.  The checksum is simply their
 * summation.
 */
static uint8_t ps_compute_checksum()
{
	int i;
	uint8_t cs = 0;
	
	for (i=0; i<PS_CHECKSUM_ADDR; i++) {
		cs += ps_shadow_buffer[i];
	}
	
	return cs;
}


/**
 * Wrapper function for gcore_set_nvram
 */
static bool ps_write_bytes_to_gcore(uint16_t start_addr, uint16_t data_len)
{
	return (gcore_set_nvram_bytes(PS_RAM_STARTADDR + start_addr, &ps_shadow_buffer[start_addr], data_len));
}


/**
 * Return an ASCII character version of a 4-bit hexadecimal number
 */
static char ps_nibble_to_ascii(uint8_t n)
{
	n = n & 0x0F;
	
	if (n < 10) {
		return '0' + n;
	} else {
		return 'A' + n - 10;
	}
}
