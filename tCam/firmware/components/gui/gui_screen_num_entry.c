/*
 * Settings number entry GUI screen related functions, callbacks and event handlers
 *
 * Copyright 2020 Dan Julio
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
#include "gui_screen_num_entry.h"
#include "gui_task.h"
#include "esp_system.h"
#include "gui_utilities.h"
#include "lv_conf.h"



//
// Number Entry GUI Screen constants
//

// Button map indicies
#define BTNM_MAP_1       0
#define BTNM_MAP_2       1
#define BTNM_MAP_3       2
#define BTNM_MAP_4       3
#define BTNM_MAP_5       4
#define BTNM_MAP_6       5
#define BTNM_MAP_7       6
#define BTNM_MAP_8       7
#define BTNM_MAP_9       8
#define BTNM_MAP_10      9
#define BTNM_MAP_CANCEL  10
#define BTNM_MAP_MINUS   11
#define BTNM_MAP_CLEAR   12
#define BTNM_MAP_BKSPC   13
#define BTNM_MAP_SAVE    14


//
// Number Entry GUI Screen variables
//

// LVGL objects
static lv_obj_t* num_entry_screen;
static lv_obj_t* lbl_num_entry_title;
static lv_obj_t* ta_num_entry;
static lv_obj_t* lbl_description;
static lv_obj_t* btn_num_entry_keypad;

// Screen state
static int cur_val;
static bool number_will_be_negative;

// Keypad array
static const char* btnm_map[] = {"1", "2", "3", "4", "5", "\n",
                                 "6", "7", "8", "9", "0", "\n",
                                 LV_SYMBOL_CLOSE, "-", "C", LV_SYMBOL_BACKSPACE, LV_SYMBOL_OK, ""};

//
// Number Entry GUI Screen internal function forward declarations
//
static void initialize_screen_values();
static void update_title();
static void update_description();
static void update_ta_num_entry();
static void limit_cur_val();
static void cb_btn_num_entry_keypad(lv_obj_t * btn, lv_event_t event);



//
// Number Entry GUI Screen API
//

/**
 * Create the number entry screen, its graphical objects and link necessary callbacks
 */
lv_obj_t* gui_screen_num_entry_create()
{
	num_entry_screen = lv_obj_create(NULL, NULL);
	lv_obj_set_size(num_entry_screen, LV_HOR_RES_MAX, LV_VER_RES_MAX);
	
	// Create the graphical elements for this screen
	//
	// Screen Title with a larger font (centered)
	lbl_num_entry_title = lv_label_create(num_entry_screen, NULL);
	static lv_style_t lbl_num_entry_title_style;
	lv_style_copy(&lbl_num_entry_title_style, gui_st.gui_theme->style.bg);
	lbl_num_entry_title_style.text.font = &lv_font_roboto_22;
	lv_label_set_style(lbl_num_entry_title, LV_LABEL_STYLE_MAIN, &lbl_num_entry_title_style);
	
	// Description String (centered)
	lbl_description = lv_label_create(num_entry_screen, NULL);
	lv_label_set_recolor(lbl_description, true);
	
	// Number entry text area
	ta_num_entry = lv_ta_create(num_entry_screen, NULL);
	lv_obj_set_pos(ta_num_entry, NE_VALUE_LBL_X, NE_VALUE_LBL_Y);
	lv_obj_set_size(ta_num_entry, NE_VALUE_LBL_W, NE_VALUE_LBL_H);
	lv_ta_set_cursor_type(ta_num_entry, LV_CURSOR_LINE);
	lv_ta_set_one_line(ta_num_entry, true);
	lv_ta_set_max_length(ta_num_entry, 6);
	
	// Modify the Number entry text area to use a larger font
	static lv_style_t ta_num_entry_style;
	lv_style_copy(&ta_num_entry_style, gui_st.gui_theme->style.bg);
	ta_num_entry_style.text.font = &lv_font_roboto_28;
	lv_label_set_style(ta_num_entry, LV_LABEL_STYLE_MAIN, &ta_num_entry_style);

	// Create the time set button matrix
	btn_num_entry_keypad = lv_btnm_create(num_entry_screen, NULL);
	lv_btnm_set_map(btn_num_entry_keypad, btnm_map);
	lv_obj_set_pos(btn_num_entry_keypad, NE_BTN_MATRIX_X, NE_BTN_MATRIX_Y);
	lv_obj_set_width(btn_num_entry_keypad, NE_BTN_MATRIX_W);
	lv_obj_set_height(btn_num_entry_keypad, NE_BTN_MATRIX_H);
	lv_btnm_set_btn_ctrl_all(btn_num_entry_keypad, LV_BTNM_CTRL_NO_REPEAT);
	lv_btnm_set_btn_ctrl_all(btn_num_entry_keypad, LV_BTNM_CTRL_CLICK_TRIG);
	lv_obj_set_event_cb(btn_num_entry_keypad, cb_btn_num_entry_keypad);

	initialize_screen_values();
		
	return num_entry_screen;
}


/**
 * Initialize the number entry screen's dynamic values
 *
 * Note: The gui global num_entry_st must have been setup prior
 */
void gui_screen_num_entry_set_active(bool en)
{
	if (en) {
		cur_val = num_entry_st.initial_val;
		num_entry_st.return_val = cur_val;
		number_will_be_negative = false;
		
		update_title();
		update_description();
		update_ta_num_entry();
	}
}


//
// Number Entry GUI Screen internal functions
//

static void initialize_screen_values()
{
	// Clear valid return flag
	num_entry_st.updated = false;
	
	// Make sure strings are null terminated
	num_entry_st.item_name[0] = 0;
	num_entry_st.description[0] = 0;
	
	// A default value for the text area's initial display
	cur_val = 0;
	number_will_be_negative = false;
	
	// Set some values for LittlevGL in case it renders the fields before updating them
	update_title();
	update_description();
	update_ta_num_entry();
}


static void update_title()
{
	static char buf[36];
	
	sprintf(buf, "Set %s", num_entry_st.item_name);
	gui_print_static_centered_text(lbl_num_entry_title, NE_TITLE_LBL_X, NE_TITLE_LBL_Y, buf);
}


static void update_description()
{
	gui_print_static_centered_text(lbl_description, NE_DESC_LBL_X, NE_DESC_LBL_Y, num_entry_st.description);
}


static void update_ta_num_entry()
{
	char buf[8];
	
	sprintf(buf, "%d", cur_val);
	lv_ta_set_text(ta_num_entry, buf);
}


static void limit_cur_val()
{
	if (cur_val < num_entry_st.min_val) cur_val = num_entry_st.min_val;
	if (cur_val > num_entry_st.max_val) cur_val = num_entry_st.max_val;
}


static void cb_btn_num_entry_keypad(lv_obj_t * btn, lv_event_t event)
{
	int button_val = -1;
	
	if (event == LV_EVENT_VALUE_CHANGED) {

		uint16_t n = lv_btnm_get_active_btn(btn);

		switch (n) {
			case BTNM_MAP_CANCEL:
				// Bail back to calling screen
				gui_set_screen(num_entry_st.calling_screen);
				break;
			
			case BTNM_MAP_SAVE:
				// Set the current value before going back to the calling screen
				limit_cur_val();
				num_entry_st.return_val = cur_val;
				num_entry_st.updated = true;
				gui_set_screen(num_entry_st.calling_screen);
				break;
			
			case BTNM_MAP_MINUS:
				if (cur_val == 0) {
					// Flag number will be negative when non-zero
					number_will_be_negative = true;
				} else {
					// Negate the current value
					cur_val = -cur_val;
					update_ta_num_entry();
				}
				break;
			
			case BTNM_MAP_CLEAR:
				cur_val = 0;
				number_will_be_negative = false;
				update_ta_num_entry();
				break;
			
			case BTNM_MAP_BKSPC:
				cur_val = cur_val / 10;
				update_ta_num_entry();
				break;
			
			default:
				// Number key
				if (n == BTNM_MAP_10) {
					// Handle '0' specially
					button_val = 0;
				} else {
					// All other numeric buttons are base-0
					button_val = n + 1;
				}
				if (cur_val < 0) {
					cur_val = cur_val*10 - button_val;
				} else {
					cur_val = cur_val*10 + button_val;
				}
				
				if (cur_val != 0) {
					if (number_will_be_negative) {
						cur_val = -cur_val;
						number_will_be_negative = false;
					}
				}
				
				if (cur_val > 32767) cur_val = 32767;
				if (cur_val < -32768) cur_val = -32768;
				update_ta_num_entry();
		}
	}
}

