/*
 * Set Time GUI screen related functions, callbacks and event handlers
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
#include "gui_screen_time.h"
#include "gui_task.h"
#include "esp_system.h"
#include "gui_utilities.h"
#include "lepton_utilities.h"
#include "time_utilities.h"
#include "lv_conf.h"
#include <time.h>
#include <sys/time.h>



//
// Set Time GUI Screen constants
//
#define TIMESET_I_HOUR_H 0
#define TIMESET_I_HOUR_L 1
#define TIMESET_I_MIN_H  2
#define TIMESET_I_MIN_L  3
#define TIMESET_I_SEC_H  4
#define TIMESET_I_SEC_L  5
#define TIMESET_I_MON_H  6
#define TIMESET_I_MON_L  7
#define TIMESET_I_DAY_H  8
#define TIMESET_I_DAY_L  9
#define TIMESET_I_YEAR_H 10
#define TIMESET_I_YEAR_L 11
#define TIMESET_NUM_I    12


// Macro to convert a single-digit numeric value (0-9) to an ASCII digit ('0' - '9')
#define ASC_DIGIT(n)     '0' + n

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
#define BTNM_MAP_LEFT    11
#define BTNM_MAP_RIGHT   12
#define BTNM_MAP_SAVE    13


//
// Set Time GUI Screen variables
//

// LVGL objects
static lv_obj_t* time_screen;
static lv_obj_t* lbl_time_title;
static lv_obj_t* lbl_time_set;
static lv_obj_t* btn_set_time_keypad;

// Time set state
static tmElements_t timeset_value;
static int timeset_index;
static char timeset_string[32];   // "HH:MM:SS MM/DD/YY" + room for #FFFFFF n# recolor string

// Keypad array
static const char* btnm_map[] = {"1", "2", "3", "4", "5", "\n",
                                 "6", "7", "8", "9", "0", "\n",
                                 LV_SYMBOL_CLOSE, LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""};
                     
// Character values to prepend the set time/date string currently indexed character with
static const char recolor_array[8] = {'#', 'F', 'F', 'F', 'F', 'F', 'F', ' '};

// Days per month (not counting leap years) for validation (0-based index)
static const uint8_t days_per_month[]={31,28,31,30,31,30,31,31,30,31,30,31};


//
// Set Time GUI Screen internal function forward declarations
//
static void fix_timeset_value();
static bool is_valid_digit_position(int i);
static void display_timeset_value();
static bool set_timeset_indexed_value(int n);
static void cb_btn_set_time_keypad(lv_obj_t * btn, lv_event_t event);



//
// Set Time GUI Screen API
//

/**
 * Create the set time screen, its graphical objects and link necessary callbacks
 */
lv_obj_t* gui_screen_time_create()
{
	time_screen = lv_obj_create(NULL, NULL);
	lv_obj_set_size(time_screen, LV_HOR_RES_MAX, LV_VER_RES_MAX);
	
	// Create the graphical elements for this screen
	//
	// Screen Title with larger font (centered)
	lbl_time_title = lv_label_create(time_screen, NULL);	
	static lv_style_t lbl_time_title_style;
	lv_style_copy(&lbl_time_title_style, gui_st.gui_theme->style.bg);
	lbl_time_title_style.text.font = &lv_font_roboto_22;
	lv_label_set_style(lbl_time_title, LV_LABEL_STYLE_MAIN, &lbl_time_title_style);
	gui_print_static_centered_text(lbl_time_title, T_TITLE_LBL_X, T_TITLE_LBL_Y, "Set Time/Date");
	
	// Set Time/Date String (centered)
	lbl_time_set = lv_label_create(time_screen, NULL);
	lv_label_set_recolor(lbl_time_set, true);
	
	// Modify the Set Time/Date string to use a larger font
	// Default color is a dimmer white so the currently selected digit can stand out
	static lv_style_t lbl_time_set_style;
	lv_style_copy(&lbl_time_set_style, gui_st.gui_theme->style.bg);
	lbl_time_set_style.text.font = &lv_font_roboto_28;
	lbl_time_set_style.text.color = LV_COLOR_MAKE(0xB0, 0xB0, 0xB0);
	lv_label_set_style(lbl_time_set, LV_LABEL_STYLE_MAIN, &lbl_time_set_style);

	// Create the time set button matrix
	btn_set_time_keypad = lv_btnm_create(time_screen, NULL);
	lv_btnm_set_map(btn_set_time_keypad, btnm_map);
	lv_obj_set_pos(btn_set_time_keypad, T_BTN_MATRIX_X, T_BTN_MATRIX_Y);
	lv_obj_set_width(btn_set_time_keypad, T_BTN_MATRIX_W);
	lv_obj_set_height(btn_set_time_keypad, T_BTN_MATRIX_H);
	lv_btnm_set_btn_ctrl_all(btn_set_time_keypad, LV_BTNM_CTRL_NO_REPEAT);
	lv_btnm_set_btn_ctrl_all(btn_set_time_keypad, LV_BTNM_CTRL_CLICK_TRIG);
	lv_obj_set_event_cb(btn_set_time_keypad, cb_btn_set_time_keypad);

	return time_screen;
}


/**
 * Initialize the time screen's dynamic values
 */
void gui_screen_time_set_active(bool en)
{
	if (en) {
		// Get the system time into our variable
		time_get(&timeset_value);
		
		// Initialize the selection index to the first digit
		timeset_index = 0;
		
		// Update the time set label
		display_timeset_value();
	}
}


//
// Set Time GUI Screen internal functions
//

/**
 * Fixup the timeset_value by recomputing it to get the correct day-of-week field
 * that we don't ask the user to set
 */
static void fix_timeset_value()
{
	time_t secs;
	
	// Convert our timeset_value into seconds (this doesn't use DOW)
	secs = rtc_makeTime(timeset_value);
	
	// Rebuild the timeset_value from the seconds so it will have a correct DOW
	rtc_breakTime(secs, &timeset_value);
}


/**
 * Returns true when the passed in index is pointing to a valid digit position and
 * not a separator character
 */
static bool is_valid_digit_position(int i)
{
	//       H  H  :  M  M  :  S  S     M  M  /  D  D  /  Y  Y
	// i     0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15 16
	return (!((i==2)||(i==5)||(i==8)||(i==11)||(i==14)));
}


/**
 * Update the set time/date label.  The current indexed digit is made to be full
 * white to indicate it is the one being changed.
 */
static void display_timeset_value()
{
	int timeset_string_index = 0;  // Current timeset_string insertion point
	int time_string_index = 0;     // Current position in displayed "HH:MM:SS MM/DD/YY"
	int time_digit_index = 0;      // Current time digit index (0-11) for HHMMSSMMDDYY
	int i;
	bool did_recolor;

	while (time_string_index <= 16) {

		// Insert the recolor string before the currently selected time digit
		if ((timeset_index == time_digit_index) && is_valid_digit_position(time_string_index)) {
			for (i=0; i<8; i++) {
				timeset_string[timeset_string_index++] = recolor_array[i];
			}
			did_recolor = true;
		} else {
			did_recolor = false;
		}
		
		// Insert the appropriate time character
		//
		//                          H  H  :  M  M  :  S  S     M  M  /  D  D  /  Y  Y
		// time_string_index        0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15 16
		// time_digit_index         0  1     2  3     4  5     6  7     8  9     10 11
		//
		switch (time_string_index++) {
			case 0: // Hours tens
				timeset_string[timeset_string_index++] = ASC_DIGIT(timeset_value.Hour / 10);
				time_digit_index++;
				break;
			case 1: // Hours units
				timeset_string[timeset_string_index++] = ASC_DIGIT(timeset_value.Hour % 10);
				time_digit_index++;
				break;
			case 3: // Minutes tens
				timeset_string[timeset_string_index++] = ASC_DIGIT(timeset_value.Minute / 10);
				time_digit_index++;
				break;
			case 4: // Minutes units
				timeset_string[timeset_string_index++] = ASC_DIGIT(timeset_value.Minute % 10);
				time_digit_index++;
				break;
			case 6: // Seconds tens
				timeset_string[timeset_string_index++] = ASC_DIGIT(timeset_value.Second / 10);
				time_digit_index++;
				break;
			case 7: // Seconds units
				timeset_string[timeset_string_index++] = ASC_DIGIT(timeset_value.Second % 10);
				time_digit_index++;
				break;
			case 9: // Month tens
				timeset_string[timeset_string_index++] = ASC_DIGIT(timeset_value.Month / 10);
				time_digit_index++;
				break;
			case 10: // Month units
				timeset_string[timeset_string_index++] = ASC_DIGIT(timeset_value.Month % 10);
				time_digit_index++;
				break;
			case 12: // Day tens
				timeset_string[timeset_string_index++] = ASC_DIGIT(timeset_value.Day / 10);
				time_digit_index++;
				break;
			case 13: // Day units
				timeset_string[timeset_string_index++] = ASC_DIGIT(timeset_value.Day % 10);
				time_digit_index++;
				break;
			case 15: // Year tens - Assume we're post 2000
				timeset_string[timeset_string_index++] = ASC_DIGIT(tmYearToY2k(timeset_value.Year) / 10);
				time_digit_index++;
				break;
			case 16: // Year units
				timeset_string[timeset_string_index++] = ASC_DIGIT(tmYearToY2k(timeset_value.Year) % 10);
				time_digit_index++;
				break;
			
			case 2: // Time section separators
			case 5:
				if (did_recolor) {
					// End recoloring before we insert this character
					timeset_string[timeset_string_index++] = '#';
					did_recolor = false;
				}
				timeset_string[timeset_string_index++] = ':';
				break;
				
			case 8: // Time / Date separator
				if (did_recolor) {
					// End recoloring before we insert this character
					timeset_string[timeset_string_index++] = '#';
					did_recolor = false;
				}
				timeset_string[timeset_string_index++] = ' ';
				break;
				
			case 11: // Date section separators
			case 14:
				if (did_recolor) {
					// End recoloring before we insert this character
					timeset_string[timeset_string_index++] = '#';
					did_recolor = false;
				}
				timeset_string[timeset_string_index++] = '/';
				break;
		}
		
		if (did_recolor) {
			// End the recoloring after the digit
			timeset_string[timeset_string_index++] = '#';
			did_recolor = false;
		}
	}
	
	// Make sure the string is terminated
	timeset_string[timeset_string_index] = 0;
	
	//lv_label_set_static_text(lbl_time_set, timeset_string);
	gui_print_static_centered_text(lbl_time_set, 240, 50, timeset_string);
}


/**
 * Apply the button press value n to the timeset_value, making sure that only
 * valid values are allowed for each digit position (for example, you cannot set
 * an hour value > 23).  Return true if the digit position was updated, false if it
 * was not changed.
 */
static bool set_timeset_indexed_value(int n)
{
	bool changed = false;
	uint8_t u8;
	
	switch (timeset_index) {
		case TIMESET_I_HOUR_H:
			if (n < 3) {
				timeset_value.Hour = (n * 10) + (timeset_value.Hour % 10);
				changed = true;
			}
			break;
		case TIMESET_I_HOUR_L:
			if (timeset_value.Hour >= 20) {
				// Only allow 20 - 23
				if (n < 4) {
					timeset_value.Hour = ((timeset_value.Hour / 10) * 10) + n;
					changed = true;
				}
			} else {
				// Allow 00-09 or 10-19
				timeset_value.Hour = ((timeset_value.Hour / 10) * 10) + n;
				changed = true;
			}
			break;
		case TIMESET_I_MIN_H:
			if (n < 6) {
				timeset_value.Minute = (n * 10) + (timeset_value.Minute % 10);
				changed = true;
			}
			break;
		case TIMESET_I_MIN_L:
			timeset_value.Minute = ((timeset_value.Minute / 10) * 10) + n;
			changed = true;
			break;
		case TIMESET_I_SEC_H:
			if (n < 6) {
				timeset_value.Second = (n * 10) + (timeset_value.Second % 10);
				changed = true;
			}
			break;
		case TIMESET_I_SEC_L:
			timeset_value.Second = ((timeset_value.Second / 10) * 10) + n;
			changed = true;
			break;
		case TIMESET_I_MON_H:
			if (n < 2) {
				timeset_value.Month = (n * 10) + (timeset_value.Month % 10);
				if (timeset_value.Month == 0) timeset_value.Month = 1;
				changed = true;
			}
			break;
		case TIMESET_I_MON_L:
			if (timeset_value.Month >= 10) {
				// Only allow 10-12
				if (n < 3) {
					timeset_value.Month = ((timeset_value.Month / 10) * 10) + n;
					changed = true;
				}
			} else {
				// Allow 1-9
				if (n > 0) {
					timeset_value.Month = ((timeset_value.Month / 10) * 10) + n;
					changed = true;
				}
			}
			break;
		case TIMESET_I_DAY_H:
			u8 = days_per_month[timeset_value.Month - 1];
			if (n <= (u8 / 10)) {
				// Only allow valid tens digit for this month (will be 2 or 3)
				timeset_value.Day = (n * 10) + (timeset_value.Day % 10);
				changed = true;
			}
			break;
		case TIMESET_I_DAY_L:
			u8 = days_per_month[timeset_value.Month - 1];
			if ((timeset_value.Day / 10) == (u8 / 10)) {
				if (n <= (u8 % 10)) {
					// Only allow valid units digits when the tens digit is the highest
					timeset_value.Day = ((timeset_value.Day / 10) * 10) + n;
					changed = true;
				}
			} else {
				// Units values of 0-9 are valid when the tens is lower than the highest
				timeset_value.Day = ((timeset_value.Day / 10) * 10) + n;
				changed = true;
			}
			break;
		case TIMESET_I_YEAR_H:
			u8 = tmYearToY2k(timeset_value.Year);
			u8 = (n * 10) + (u8 % 10);
			timeset_value.Year = y2kYearToTm(u8);
			changed = true;
			break;
		case TIMESET_I_YEAR_L:
			u8 = tmYearToY2k(timeset_value.Year);
			u8 = ((u8 / 10) * 10) + n;
			timeset_value.Year = y2kYearToTm(u8);
			changed = true;
			break;
	}

	return changed;
}


static void cb_btn_set_time_keypad(lv_obj_t * btn, lv_event_t event)
{
	int button_val = -1;
	
	if (event == LV_EVENT_VALUE_CHANGED) {

		uint16_t n = lv_btnm_get_active_btn(btn);
	
		if (n == BTNM_MAP_CANCEL) {
			// Bail back to settings screen
			gui_set_screen(GUI_SCREEN_SETTINGS);
		} else if (n == BTNM_MAP_SAVE) {
			// Set the time before going back to the settings screen
			fix_timeset_value();
			time_set(timeset_value);
			
			// Update the clock in tCamMini after we've updated our own time
			lepton_set_time();
		
			gui_set_screen(GUI_SCREEN_SETTINGS);
		} else if (n == BTNM_MAP_LEFT) {
			// Decrement to the previous digit
			if (timeset_index > TIMESET_I_HOUR_H) {
				timeset_index--;
			}
			display_timeset_value();
		} else if (n == BTNM_MAP_RIGHT) {
			// Increment to the next digit
			if (timeset_index < TIMESET_I_YEAR_L) {
				timeset_index++;
			}
			display_timeset_value();
		} else if (n <= BTNM_MAP_10) {
			// Number button
			if (n == BTNM_MAP_10) {
				// Handle '0' specially
				button_val = 0;
			} else {
				// All other numeric buttons are base-0
				button_val = n + 1;
			}

			// Update the indexed digit based on the button value
			if (set_timeset_indexed_value(button_val)) {
				// Increment to next digit if the digit was changed
				if (timeset_index < TIMESET_I_YEAR_L) {
					timeset_index++;
				}
			}
			
			// Update the display
			display_timeset_value();
		}
	}
}

