/*
 * Set Network IP GUI screen related functions, callbacks and event handlers
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
#include "gui_screen_network.h"
#include "app_task.h"
#include "gui_task.h"
#include "esp_system.h"
#include "gui_utilities.h"
#include "ps_utilities.h"
#include "sys_utilities.h"
#include "wifi_utilities.h"
#include "lv_conf.h"



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
#define BTNM_MAP_BSP     13
#define BTNM_MAP_SAVE    14


//
// Network GUI Screen variables
//

// LVGL objects
static lv_obj_t* network_screen;
static lv_obj_t* lbl_network_title;
static lv_obj_t* cb_static_enable;
static lv_obj_t* ta_ip_entry;
static lv_obj_t* lbl_ip_entry;
static lv_obj_t* ta_netmask_entry;
static lv_obj_t* lbl_netmask_entry;
static lv_obj_t* btn_set_network_keypad;

// Screen state
static bool netmask_ta_selected;

static char ip_edit_string[16];        // Room for "XXX.XXX.XXX.XXX" + null
static char netmask_edit_string[16];   // Room for "XXX.XXX.XXX.XXX" + null
static int cur_ip_byte_index;          // 0 = least significant byte, 3 = most significant byte
static int cur_ip_byte_val;
static char wifi_ap_ssid_array[PS_SSID_MAX_LEN+1];
static char wifi_sta_ssid_array[PS_SSID_MAX_LEN+1];
static char wifi_ap_pw_array[PS_PW_MAX_LEN+1];
static char wifi_sta_pw_array[PS_PW_MAX_LEN+1];
static wifi_info_t local_wifi_info = {
	wifi_ap_ssid_array,
	wifi_sta_ssid_array,
	wifi_ap_pw_array,
	wifi_sta_pw_array,
	0,
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0}
};


// Keypad array
static const char* btnm_map[] = {"1", "2", "3", "4", "5", "\n",
                                 "6", "7", "8", "9", "0", "\n",
                                 LV_SYMBOL_CLOSE, LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, LV_SYMBOL_BACKSPACE, LV_SYMBOL_OK, ""};


//
// Network GUI Screen internal function forward declarations
//
static void set_netmask_focus(bool en);
static void update_ip_ta();
static void update_netmask_ta();
static int make_valid_ip_num(int n);
static int chars_in_num(uint8_t n);
static void cb_static_enable_checkbox(lv_obj_t * cb, lv_event_t event);
static void cb_ip_entry(lv_obj_t * cb, lv_event_t event);
static void cb_netmask_entry(lv_obj_t * cb, lv_event_t event);
static void cb_btn_set_network_keypad(lv_obj_t * btn, lv_event_t event);


//
// Network GUI Screen API
//

/**
 * Create the network screen, its graphical objects and link necessary callbacks
 */
lv_obj_t* gui_screen_network_create()
{
	network_screen = lv_obj_create(NULL, NULL);
	lv_obj_set_size(network_screen, LV_HOR_RES_MAX, LV_VER_RES_MAX);
	
	// Create the graphical elements for this screen
	//
	// Screen Title with a larger font (centered)
	lbl_network_title = lv_label_create(network_screen, NULL);;
	static lv_style_t lbl_settings_title_style;
	lv_style_copy(&lbl_settings_title_style, gui_st.gui_theme->style.bg);
	lbl_settings_title_style.text.font = &lv_font_roboto_22;
	lv_label_set_style(lbl_network_title, LV_LABEL_STYLE_MAIN, &lbl_settings_title_style);
	gui_print_static_centered_text(lbl_network_title, N_TITLE_LBL_X, N_TITLE_LBL_Y, "Client Static IP Address");
	
	// Enable client mode checkbox
	cb_static_enable = lv_cb_create(network_screen, NULL);
	lv_obj_set_pos(cb_static_enable, N_EN_CB_X, N_EN_CB_Y);
	lv_obj_set_width(cb_static_enable, N_EN_CB_W);
	lv_cb_set_static_text(cb_static_enable, "Enable");
	lv_obj_set_event_cb(cb_static_enable, cb_static_enable_checkbox);
	
	// IP entry text area
	ta_ip_entry = lv_ta_create(network_screen, NULL);
	lv_obj_set_pos(ta_ip_entry, N_IP_TA_X, N_IP_TA_Y);
	lv_obj_set_width(ta_ip_entry, N_IP_TA_W);
	lv_ta_set_text_align(ta_ip_entry, LV_LABEL_ALIGN_LEFT);
	lv_ta_set_one_line(ta_ip_entry, true);
	lv_ta_set_cursor_click_pos(ta_ip_entry, false);
	lv_ta_set_max_length(ta_ip_entry, 15);
	lv_obj_set_event_cb(ta_ip_entry, cb_ip_entry);
	
	// IP entry label
	lbl_ip_entry = lv_label_create(network_screen, NULL);
	lv_obj_set_pos(lbl_ip_entry, N_IP_LBL_X, N_IP_LBL_Y);
	lv_label_set_static_text(lbl_ip_entry, "IP Address");
	
	// Netmask entry text area
	ta_netmask_entry = lv_ta_create(network_screen, NULL);
	lv_obj_set_pos(ta_netmask_entry, N_NM_TA_X, N_NM_TA_Y);
	lv_obj_set_width(ta_netmask_entry, N_NM_TA_W);
	lv_ta_set_text_align(ta_netmask_entry, LV_LABEL_ALIGN_LEFT);
	lv_ta_set_one_line(ta_netmask_entry, true);
	lv_ta_set_cursor_click_pos(ta_netmask_entry, false);
	lv_ta_set_max_length(ta_netmask_entry, 15);
	lv_obj_set_event_cb(ta_netmask_entry, cb_netmask_entry);
	
	// Netmask entry label
	lbl_netmask_entry = lv_label_create(network_screen, NULL);
	lv_obj_set_pos(lbl_netmask_entry, N_NM_LBL_X, N_NM_LBL_Y);
	lv_label_set_static_text(lbl_netmask_entry, "Netmask");

	// Create the time set button matrix
	btn_set_network_keypad = lv_btnm_create(network_screen, NULL);
	lv_btnm_set_map(btn_set_network_keypad, btnm_map);
	lv_obj_set_pos(btn_set_network_keypad, N_BTN_MATRIX_X, N_BTN_MATRIX_Y);
	lv_obj_set_width(btn_set_network_keypad, N_BTN_MATRIX_W);
	lv_obj_set_height(btn_set_network_keypad, N_BTN_MATRIX_H);
	lv_btnm_set_btn_ctrl_all(btn_set_network_keypad, LV_BTNM_CTRL_NO_REPEAT);
	lv_btnm_set_btn_ctrl_all(btn_set_network_keypad, LV_BTNM_CTRL_CLICK_TRIG);
	lv_obj_set_event_cb(btn_set_network_keypad, cb_btn_set_network_keypad);

	return network_screen;
}


/**
 * Initialize the network screen's dynamic values
 */
void gui_screen_network_set_active(bool en)
{
	if (en) {
		// Get local copies of the state
		ps_get_wifi_info(&local_wifi_info);
		
		// Set edit focus to IP Address
		set_netmask_focus(false);
		
		// Initialize the selection index to the first digit
		cur_ip_byte_index = 0;
		cur_ip_byte_val = local_wifi_info.sta_ip_addr[cur_ip_byte_index];
		
		// Update the static ip enable checkbox
		lv_cb_set_checked(cb_static_enable, (local_wifi_info.flags & WIFI_INFO_FLAG_CL_STATIC_IP) != 0);
		
		// Update the IP set text area
		update_ip_ta();
		update_netmask_ta();
	}
}


//
// Network GUI Screen internal functions
//

static void set_netmask_focus(bool en)
{
	netmask_ta_selected = en;
	
	cur_ip_byte_index = 0;
	
	if (en) {
		lv_ta_set_cursor_type(ta_ip_entry, LV_CURSOR_NONE);
		lv_ta_set_cursor_type(ta_netmask_entry, LV_CURSOR_LINE);
		cur_ip_byte_val = local_wifi_info.sta_netmask[cur_ip_byte_index];
		update_netmask_ta();
	} else {
		lv_ta_set_cursor_type(ta_ip_entry, LV_CURSOR_LINE);
		lv_ta_set_cursor_type(ta_netmask_entry, LV_CURSOR_NONE);
		cur_ip_byte_val = local_wifi_info.sta_ip_addr[cur_ip_byte_index];
		update_ip_ta();
	}
}


static void update_ip_ta()
{
	int i = 3;
	int16_t n;
	
	// Update text contents
	sprintf(ip_edit_string, "%d.%d.%d.%d", local_wifi_info.sta_ip_addr[3],
	                                       local_wifi_info.sta_ip_addr[2],
	                                       local_wifi_info.sta_ip_addr[1],
	                                       local_wifi_info.sta_ip_addr[0] );
	                                       
	lv_ta_set_text(ta_ip_entry, ip_edit_string);
	
	// Determine current cursor position
	n = 0;
	while (i >= cur_ip_byte_index) {
		n += chars_in_num(local_wifi_info.sta_ip_addr[i]);
		if (i == cur_ip_byte_index) {
			// Done: Cursor at end of this field
			break;
		}
		n += 1;  // Include decimal point character
		i--;
	}
	lv_ta_set_cursor_pos(ta_ip_entry, n);
}


static void update_netmask_ta()
{
	int i = 3;
	int16_t n;
	
	// Update text contents
	sprintf(netmask_edit_string, "%d.%d.%d.%d", local_wifi_info.sta_netmask[3],
	                                            local_wifi_info.sta_netmask[2],
	                                            local_wifi_info.sta_netmask[1],
	                                            local_wifi_info.sta_netmask[0] );
	                                       
	lv_ta_set_text(ta_netmask_entry, netmask_edit_string);
	
	// Determine current cursor position
	n = 0;
	while (i >= cur_ip_byte_index) {
		n += chars_in_num(local_wifi_info.sta_netmask[i]);
		if (i == cur_ip_byte_index) {
			// Done: Cursor at end of this field
			break;
		}
		n += 1;  // Include decimal point character
		i--;
	}
	lv_ta_set_cursor_pos(ta_netmask_entry, n);
}


static int make_valid_ip_num(int n)
{
	if (n > 255) {
		return 255;
	} else {
		return (uint8_t) n;
	}
}


static int chars_in_num(uint8_t n)
{
	if (n < 10) {
		return 1;
	} else if (n < 100) {
		return 2;
	}
	
	return 3;
}


static void cb_static_enable_checkbox(lv_obj_t * cb, lv_event_t event)
{
	if (event == LV_EVENT_VALUE_CHANGED) {
		if (lv_cb_is_checked(cb)) {
			local_wifi_info.flags |= WIFI_INFO_FLAG_CL_STATIC_IP;
		} else {
			local_wifi_info.flags &= ~WIFI_INFO_FLAG_CL_STATIC_IP;
		}
	}
}


static void cb_ip_entry(lv_obj_t * cb, lv_event_t event)
{
	if (event == LV_EVENT_PRESSED) {
		set_netmask_focus(false);
	}
}


static void cb_netmask_entry(lv_obj_t * cb, lv_event_t event)
{
	if (event == LV_EVENT_PRESSED) {
		set_netmask_focus(true);
	}
}


static void cb_btn_set_network_keypad(lv_obj_t * btn, lv_event_t event)
{
	int button_val = -1;
	
	if (event == LV_EVENT_VALUE_CHANGED) {

		uint16_t n = lv_btnm_get_active_btn(btn);
	
		if (n == BTNM_MAP_CANCEL) {
			// Bail back to settings screen
			gui_set_screen(GUI_SCREEN_SETTINGS);
		} else if (n == BTNM_MAP_SAVE) {
			// Save the new WiFi configuration
			ps_set_wifi_info(&local_wifi_info);
			// Notify app_task of the update
			xTaskNotify(task_handle_app, APP_NOTIFY_NEW_WIFI_MASK, eSetBits);
			// Return to the settings screen
			gui_set_screen(GUI_SCREEN_SETTINGS);
		} else if (n == BTNM_MAP_LEFT) {
			// Increment to the next higher byte (left)
			if (cur_ip_byte_index < 3) {
				cur_ip_byte_index++;
				if (netmask_ta_selected) {
					cur_ip_byte_val = local_wifi_info.sta_netmask[cur_ip_byte_index];
					update_netmask_ta();
				} else {
					cur_ip_byte_val = local_wifi_info.sta_ip_addr[cur_ip_byte_index];
					update_ip_ta();
				}
			}
		} else if (n == BTNM_MAP_RIGHT) {
			// Decrement to the next lower byte (right)
			if (cur_ip_byte_index > 0) {
				cur_ip_byte_index--;
				if (netmask_ta_selected) {
					cur_ip_byte_val = local_wifi_info.sta_netmask[cur_ip_byte_index];
					update_netmask_ta();
				} else {
					cur_ip_byte_val = local_wifi_info.sta_ip_addr[cur_ip_byte_index];
					update_ip_ta();
				}
			}
		} else if (n == BTNM_MAP_BSP) {
			cur_ip_byte_val = cur_ip_byte_val / 10;
			if (netmask_ta_selected) {
				local_wifi_info.sta_netmask[cur_ip_byte_index] = (uint8_t) cur_ip_byte_val;
				update_netmask_ta();
			} else {
				local_wifi_info.sta_ip_addr[cur_ip_byte_index] = (uint8_t) cur_ip_byte_val;
				update_ip_ta();
			}
		} else if (n <= BTNM_MAP_10) {
			// Number button
			if (n == BTNM_MAP_10) {
				// Handle '0' specially
				button_val = 0;
			} else {
				// All other numeric buttons are base-0
				button_val = n + 1;
			}

			// Update the current byte based on the button value
			cur_ip_byte_val = (cur_ip_byte_val * 10) + button_val;
			cur_ip_byte_val = make_valid_ip_num(cur_ip_byte_val);
			if (netmask_ta_selected) {
				local_wifi_info.sta_netmask[cur_ip_byte_index] = (uint8_t) cur_ip_byte_val;
				update_netmask_ta();
			} else {
				local_wifi_info.sta_ip_addr[cur_ip_byte_index] = (uint8_t) cur_ip_byte_val;
				update_ip_ta();
			}
		}
	}
}

