/*
 * Utility functions and data structures for all screens
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
#ifndef GUI_UTILITIES_H
#define GUI_UTILITIES_H

#include <stdbool.h>
#include <stdint.h>
#include "lvgl/lvgl.h"

//
// GUI Utilities Constants
//

// Message box button ids
#define GUI_MSG_BOX_BTN_NONE    LV_BTNM_BTN_NONE
#define GUI_MSG_BOX_BTN_DISMSS  0
#define GUI_MSG_BOX_BTN_AFFIRM  1

// Message box dimensions
#define GUI_MSG_BOX_W           240
#define GUI_MSG_BOX_H           180

// Maximum preset message box string length
#define GUI_MSG_BOX_MAX_LEN     128


//
// GUI Typedefs
//


// GUI state - state shared between screens
typedef struct {
	bool agc_enabled;            // Set by telemetry from Lepton to indicate image state
	bool display_interp_enable;
	bool is_radiometric;         // Set by telemetry from Lepton to indicate if the lepton is radiometric
	bool spotmeter_enable;
	bool temp_unit_C;
	bool man_range_mode;         // Manual range (when camera is in Radiometric mode)
	bool rad_high_res;           // Set by telem when radiometric resolution is 0.01, clear when 0.1
	bool record_mode;
	bool recording;
	int lcd_brightness;          // Brightness percent (0-100)
	int man_range_min;           // Degree K * 100
	int man_range_max;           // Degree K * 100
	int palette;
	int recording_interval;      // mSec between video images; 0=fastest possible
	float fps;                   // Displayed frames/second
	lv_theme_t* gui_theme;
} gui_state_t;


// Number-entry screen data structure so it can communicate with calling screens
typedef struct {
	bool updated;
	char item_name[32];
	char description[64];           
	int item_num;
	int initial_val;
	int min_val;
	int max_val;
	int return_val;
	int calling_screen;
} gui_num_entry_screen_st_t;


// Lepton information - obtained from telemetry packets by gui_screen_main
typedef struct {
	bool valid;
	uint16_t lep_fpa_temp_k100;
	uint16_t lep_housing_temp_k100;
	uint16_t lep_version[4];
} lep_into_t;



//
// Global GUI state
//
extern gui_state_t gui_st;
extern gui_num_entry_screen_st_t num_entry_st;
extern lep_into_t lep_info;


//
// GUI Utilities API
//
void gui_state_init();

bool gui_tel_is_radiometric(uint16_t* tel);

void gui_preset_message_box_string(const char* msg, bool dual_btn);
void gui_preset_message_box(lv_obj_t* parent);
void gui_close_message_box();
bool gui_message_box_displayed();

void gui_print_static_centered_text(lv_obj_t* obj, int x, int y, char* s);
float gui_lep_to_disp_temp(uint16_t v, bool rad_high_res);
float gui_range_val_to_disp_temp(int v);
void gui_sprintf_temp_string(char* buf, float t);
int gui_man_range_val_to_lep(int gui_val);

#define gui_get_gui_st() (&gui_st)

#endif /* GUI_UTILITIES_H */