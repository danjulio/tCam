/*
 * Wifi Configuration GUI screen related functions, callbacks and event handlers
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
#include "gui_screen_wifi.h"
#include "app_task.h"
#include "cmd_task.h"
#include "gui_task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "gui_utilities.h"
#include "ps_utilities.h"
#include "sys_utilities.h"
#include "wifi_utilities.h"
#include "lvgl/lvgl.h"
#include "lv_conf.h"
#include <string.h>



//
// Set WiFi GUI Screen private constants
//

// Currently selected text area
enum selected_text_area_t {
	SET_SSID,
	SET_PW
};



//
// Set Wifi GUI Screen variables
//

static const char* TAG = "gui_screen_wifi";

// LVGL objects
static lv_obj_t* wifi_screen;
static lv_obj_t* lbl_wifi_title;
static lv_obj_t* cb_ap_en;
static lv_obj_t* cb_wifi_en;
static lv_obj_t* lbl_ssid;
static lv_obj_t* ta_ssid;
static lv_obj_t* lbl_pw;
static lv_obj_t* ta_pw;
static lv_obj_t* btn_scan_wifi;
static lv_obj_t* lbl_btn_scan_wifi;
static lv_obj_t* btn_show_password;
static lv_obj_t* lbl_btn_show_password;
static lv_obj_t* pl_scanning;
static lv_obj_t* page_tbl_ssid_list;
static lv_obj_t* tbl_ssid_list;
static lv_obj_t* kbd;

// Scan task
static lv_task_t* scan_task;

// Screen WiFi information
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

// Screen state
static bool wifi_scan_active;
static bool force_sta_pw_hidden;
static enum selected_text_area_t selected_text_area_index;
static lv_obj_t* selected_text_area_lv_obj;
static int selected_text_area_max_chars;

// Scrolling SSID List
static uint16_t tbl_w;                    // Determined by scrolling page

// Pointer to a list of wifi_ap_record_t structures returned by the WiFi module after a scan
wifi_ap_record_t *ap_infoP = NULL;



//
// Set WiFi GUI Screen Forward Declarations for internal functions
//
static void update_values_from_ps();
static void setup_wifi_scan();
static void stop_wifi_scan();
static void set_active_text_area(enum selected_text_area_t n);
static void set_scan_icon();
static void set_show_password_icon();
static void cb_wifi_en_checkbox(lv_obj_t* cb, lv_event_t event);
static void cb_ap_en_checkbox(lv_obj_t* cb, lv_event_t event);
static void cb_ssid_ta(lv_obj_t* ta, lv_event_t event);
static void cb_pw_ta(lv_obj_t* ta, lv_event_t event);
static void cb_scan_wifi_btn(lv_obj_t* ta, lv_event_t event);
static void cb_show_pw_btn(lv_obj_t* lbl, lv_event_t event);
static void cb_tbl_ssid_list(lv_obj_t* lbl, lv_event_t event);
static void cb_kbd(lv_obj_t* kb, lv_event_t event);
static void task_wifi_scan(lv_task_t* task);



//
// Set WiFi GUI Screen API
//
lv_obj_t* gui_screen_wifi_create()
{
	wifi_screen = lv_obj_create(NULL, NULL);
	lv_obj_set_size(wifi_screen, LV_HOR_RES_MAX, LV_VER_RES_MAX);
	
	// Create the graphical elements for this screen
	//
	// Screen Title
	lbl_wifi_title = lv_label_create(wifi_screen, NULL);;
	static lv_style_t lbl_settings_title_style;
	lv_style_copy(&lbl_settings_title_style, gui_st.gui_theme->style.bg);
	lbl_settings_title_style.text.font = &lv_font_roboto_22;
	lv_label_set_style(lbl_wifi_title, LV_LABEL_STYLE_MAIN, &lbl_settings_title_style);
	gui_print_static_centered_text(lbl_wifi_title, W_TITLE_LBL_X, W_TITLE_LBL_Y, "WiFi Settings");
		
	// WiFi Enable checkbox
	cb_wifi_en = lv_cb_create(wifi_screen, NULL);
	lv_obj_set_pos(cb_wifi_en, W_EN_CB_X, W_EN_CB_Y);
	lv_obj_set_width(cb_wifi_en, W_EN_CB_W);
	lv_cb_set_static_text(cb_wifi_en, "Enable");
	lv_obj_set_event_cb(cb_wifi_en, cb_wifi_en_checkbox);
	
	// Access Point Enable checkbox
	cb_ap_en = lv_cb_create(wifi_screen, NULL);
	lv_obj_set_pos(cb_ap_en, W_AP_EN_CB_X, W_AP_EN_CB_Y);
	lv_obj_set_width(cb_ap_en, W_AP_EN_CB_W);
	lv_cb_set_static_text(cb_ap_en, "AP");
	lv_obj_set_event_cb(cb_ap_en, cb_ap_en_checkbox);
	
	// WiFi SSID text entry area label
	lbl_ssid = lv_label_create(wifi_screen, NULL);
	lv_obj_set_pos(lbl_ssid, W_SSID_LBL_X, W_SSID_LBL_Y);
	lv_label_set_static_text(lbl_ssid, "SSID");
	
	// WiFi SSID text entry text area
	ta_ssid = lv_ta_create(wifi_screen, NULL);
	lv_obj_set_pos(ta_ssid, W_SSID_TA_X, W_SSID_TA_Y);
	lv_obj_set_width(ta_ssid, W_SSID_TA_W);
	lv_ta_set_text_align(ta_ssid, LV_LABEL_ALIGN_LEFT);
	lv_ta_set_one_line(ta_ssid, true);
	lv_ta_set_max_length(ta_ssid, PS_SSID_MAX_LEN);
	lv_obj_set_event_cb(ta_ssid, cb_ssid_ta);
	
	// WiFi Password text entry area label
	lbl_pw = lv_label_create(wifi_screen, NULL);
	lv_obj_set_pos(lbl_pw, W_PW_LBL_X, W_PW_LBL_Y);
	lv_label_set_static_text(lbl_pw, "PW");
	
	// WiFi Password text entry text area
	ta_pw = lv_ta_create(wifi_screen, NULL);
	lv_obj_set_pos(ta_pw, W_PW_TA_X, W_PW_TA_Y);
	lv_obj_set_width(ta_pw, W_PW_TA_W);
	lv_ta_set_text_align(ta_pw, LV_LABEL_ALIGN_LEFT);
	lv_ta_set_one_line(ta_pw, true);
	lv_ta_set_max_length(ta_pw, PS_PW_MAX_LEN);
	lv_ta_set_pwd_mode(ta_pw, true);
	lv_obj_set_event_cb(ta_pw, cb_pw_ta);
	
	// Scan Wifi button
	btn_scan_wifi = lv_btn_create(wifi_screen, NULL);
	lv_obj_set_pos(btn_scan_wifi, W_SCAN_BTN_X, W_SCAN_BTN_Y);
	lv_obj_set_width(btn_scan_wifi, W_SCAN_BTN_W);
	lv_obj_set_height(btn_scan_wifi, W_SCAN_BTN_H);
	lv_obj_set_event_cb(btn_scan_wifi, cb_scan_wifi_btn);
	
	// Scan Wifi button label
	lbl_btn_scan_wifi = lv_label_create(btn_scan_wifi, NULL);
	lv_label_set_recolor(lbl_btn_scan_wifi, true);
	lv_label_set_static_text(lbl_btn_scan_wifi, LV_SYMBOL_REFRESH);
	
	// Show password button
	btn_show_password = lv_btn_create(wifi_screen, NULL);
	lv_obj_set_pos(btn_show_password, W_PW_BTN_X, W_PW_BTN_Y);
	lv_obj_set_width(btn_show_password, W_PW_BTN_W);
	lv_obj_set_height(btn_show_password, W_PW_BTN_H);
	lv_obj_set_event_cb(btn_show_password, cb_show_pw_btn);
	
	// Show password button label
	lbl_btn_show_password = lv_label_create(btn_show_password, NULL);
	lv_label_set_recolor(lbl_btn_show_password, true);
	lv_label_set_static_text(lbl_btn_show_password, LV_SYMBOL_EYE_CLOSE);
	
	// Preloader to be displayed when scanning for wifi access points
	pl_scanning = lv_preload_create(wifi_screen, NULL);
	lv_obj_set_pos(pl_scanning, W_SSID_TBL_X + (W_SSID_TBL_W/2) - 15, 5);
    lv_obj_set_size(pl_scanning, W_SCAN_PL_W, W_SCAN_PL_H);
    lv_preload_set_type(pl_scanning, LV_PRELOAD_TYPE_FILLSPIN_ARC);
    lv_obj_set_hidden(pl_scanning, true);
	
	// Scrollable background page for SSID List Table with no internal body padding
	page_tbl_ssid_list = lv_page_create(wifi_screen, NULL);
	static lv_style_t page_bg_style;
	lv_style_copy(&page_bg_style, gui_st.gui_theme->style.bg);
	lv_obj_set_pos(page_tbl_ssid_list, W_SSID_TBL_X, W_SSID_TBL_Y);
	lv_obj_set_size(page_tbl_ssid_list, W_SSID_TBL_W, W_SSID_TBL_H);
	page_bg_style.body.padding.top = 0;
	page_bg_style.body.padding.bottom = 0;
	page_bg_style.body.padding.left = 0;
	page_bg_style.body.padding.right = 0;
	page_bg_style.body.border.width = 0;
	lv_page_set_style(page_tbl_ssid_list, LV_PAGE_STYLE_BG, &page_bg_style);
	lv_obj_set_event_cb(page_tbl_ssid_list, cb_tbl_ssid_list);
	tbl_w = lv_page_get_fit_width(page_tbl_ssid_list); 
	
	// SSID List for wifi access points
	tbl_ssid_list = lv_table_create(page_tbl_ssid_list, NULL);
	lv_table_set_col_cnt(tbl_ssid_list, 1);
	lv_table_set_col_width(tbl_ssid_list, 0, tbl_w - 1);
    lv_obj_set_pos(tbl_ssid_list, (W_SSID_TBL_W - tbl_w)/2, 0);
	
	// Keyboard
	kbd = lv_kb_create(wifi_screen, NULL);
    lv_kb_set_cursor_manage(kbd, true);
    lv_obj_align(kbd, NULL, LV_ALIGN_IN_BOTTOM_MID, 0, 0);
    lv_obj_set_event_cb(kbd, cb_kbd);
    
    set_active_text_area(SET_SSID);
    
    wifi_scan_active = false;
    force_sta_pw_hidden = true;
    scan_task = NULL;
		
	return wifi_screen;
}


/**
 * Initialize the time screen's dynamic values
 */
void gui_screen_wifi_set_active(bool en)
{
	if (en) {
		// Get the current WiFi values into our value and update our controls
		update_values_from_ps();
		set_active_text_area(SET_SSID);
		set_scan_icon();
		
		force_sta_pw_hidden = true;
		lv_ta_set_pwd_mode(ta_pw, true);  // Password always starts hidden
		set_show_password_icon();
	}
}



//
// Set WiFi GUI Screen internal functions
//

/**
 * Get the system values and update our controls
 */
static void update_values_from_ps()
{
	ps_get_wifi_info(&local_wifi_info);
	
	if ((local_wifi_info.flags & WIFI_INFO_FLAG_CLIENT_MODE) != 0) {
		lv_ta_set_text(ta_ssid, local_wifi_info.sta_ssid);
		lv_ta_set_text(ta_pw, local_wifi_info.sta_pw);	
	} else {
		lv_ta_set_text(ta_ssid, local_wifi_info.ap_ssid);
		lv_ta_set_text(ta_pw, local_wifi_info.ap_pw);
	}
	
	lv_cb_set_checked(cb_ap_en, (local_wifi_info.flags & WIFI_INFO_FLAG_CLIENT_MODE) == 0);
	lv_cb_set_checked(cb_wifi_en, (local_wifi_info.flags & WIFI_INFO_FLAG_STARTUP_ENABLE) != 0);
}


/**
 * Setup GUI and start a wifi scan - note this disconnects any existing wifi connections
 */
static void setup_wifi_scan()
{	
	// Trigger the WiFi module to start scanning
	wifi_scan_active = wifi_setup_scan();
	if (wifi_scan_active) {
		// Clear the SSID list
		lv_table_set_row_cnt(tbl_ssid_list, 0);
		
		// Make the spinner visible
		lv_obj_set_hidden(pl_scanning, false);
		
		// Start our monitoring task (deleting it first if it is already running - which it shouldn't be)
		if (scan_task != NULL) {
			lv_task_del(scan_task);
			scan_task = NULL;
		}
		scan_task = lv_task_create(task_wifi_scan, 250, LV_TASK_PRIO_LOW, NULL);
	} else {
		gui_preset_message_box_string("Could not start SSID Scan", false);
		xTaskNotify(task_handle_gui, GUI_NOTIFY_MESSAGEBOX_MASK, eSetBits);
	}
}


/**
 * Stop an on-going scan and restart with existing WiFi state
 */
static void stop_wifi_scan()
{
	if (wifi_scan_active) {
		// Stop the WiFi module's scan process
		wifi_scan_active = false;
		wifi_stop_scan();
		
		// Hide the spinner
		lv_obj_set_hidden(pl_scanning, true);
		
		// Restart WiFi if necessary (so user can exit without saving with no change to WiFi operation)
		if (!wifi_reinit()) {
			ESP_LOGE(TAG, "Could not restart WiFi after stopping scan");
		}
	}
}


/**
 * Select the currently active text area, set its cursor at the end and
 * connect it to the keyboard.  Make the cursor invisible in the other text
 * area.  Blank the password if it is the first time the text area is clicked
 * and displaying a [hidden] station password.
 */
void set_active_text_area(enum selected_text_area_t n)
{
	selected_text_area_index = n;
	
	if (n == SET_SSID) {
		selected_text_area_lv_obj = ta_ssid;
		selected_text_area_max_chars = PS_SSID_MAX_LEN;
		lv_ta_set_cursor_type(ta_ssid, LV_CURSOR_LINE);
		lv_ta_set_cursor_type(ta_pw, LV_CURSOR_LINE | LV_CURSOR_HIDDEN);
	} else {
		selected_text_area_lv_obj = ta_pw;
		selected_text_area_max_chars = PS_PW_MAX_LEN;
		lv_ta_set_cursor_type(ta_ssid, LV_CURSOR_LINE | LV_CURSOR_HIDDEN);
		lv_ta_set_cursor_type(ta_pw, LV_CURSOR_LINE);
		
		if (force_sta_pw_hidden && ((local_wifi_info.flags & WIFI_INFO_FLAG_CLIENT_MODE) != 0)) {
			force_sta_pw_hidden = false;
			lv_ta_set_text(ta_pw, "");  // display blank PW
			set_show_password_icon();   // Update button
		}
	}
	
	lv_ta_set_cursor_pos(selected_text_area_lv_obj, LV_TA_CURSOR_LAST);	
	lv_kb_set_ta(kbd, selected_text_area_lv_obj);
}


/**
 * Update the Scan button label and button click detection
 */
static void set_scan_icon()
{
	static char view_buf[13];   // Statically allocated, big enough for recolor symbol + symbol '#XXXXXX sss#0'
	
	// Enable scan button if in client mode
	if ((local_wifi_info.flags & WIFI_INFO_FLAG_CLIENT_MODE) != 0) {
		sprintf(view_buf, "#FFFFFF " LV_SYMBOL_REFRESH "#");
		lv_obj_set_click(btn_scan_wifi, true);
	} else {
		sprintf(view_buf, "#808080 " LV_SYMBOL_REFRESH "#");
		lv_obj_set_click(btn_scan_wifi, false);
	}
	
	lv_label_set_static_text(lbl_btn_scan_wifi, view_buf);
}


/**
 * Update the Show Password button label based on the current text area show password state
 */
static void set_show_password_icon()
{
	static char view_buf[13];   // Statically allocated, big enough for recolor symbol + symbol '#XXXXXX sss#0'
	
	// Button click enable if AP mode or not hidden STA mode
	if (!force_sta_pw_hidden || ((local_wifi_info.flags & WIFI_INFO_FLAG_CLIENT_MODE) == 0)) {
		// Button enabled
		lv_obj_set_click(btn_show_password, true);
		if (lv_ta_get_pwd_mode(ta_pw)) {
			sprintf(view_buf, "#FFFFFF " LV_SYMBOL_EYE_CLOSE "#");
		} else {
			sprintf(view_buf, "#FFFFFF " LV_SYMBOL_EYE_OPEN "#");
		}
	} else {
		// Button disabled
		lv_obj_set_click(btn_show_password, false);
		if (lv_ta_get_pwd_mode(ta_pw)) {
			sprintf(view_buf, "#808080 " LV_SYMBOL_EYE_CLOSE "#");
		} else {
			sprintf(view_buf, "#808080 " LV_SYMBOL_EYE_OPEN "#");
		}
	}
	
	lv_label_set_static_text(lbl_btn_show_password, view_buf);
}


/**
 * Wifi Enable Checkbox handler
 */
static void cb_wifi_en_checkbox(lv_obj_t* cb, lv_event_t event)
{
	if (event == LV_EVENT_VALUE_CHANGED) {
		if (lv_cb_is_checked(cb)) {
			local_wifi_info.flags |= WIFI_INFO_FLAG_STARTUP_ENABLE;
		} else {
			local_wifi_info.flags &= ~WIFI_INFO_FLAG_STARTUP_ENABLE;
		}
	}
}


/**
 * Access Point Enable Checkbox handler
 */
static void cb_ap_en_checkbox(lv_obj_t* cb, lv_event_t event)
{
	if (event == LV_EVENT_VALUE_CHANGED) {
		// Get the current strings
		char* ssid_ta_str = (char*) lv_ta_get_text(ta_ssid);
		char* pw_ta_str = (char*) lv_ta_get_text(ta_pw);
		
		if (lv_cb_is_checked(cb)) {
			local_wifi_info.flags &= ~WIFI_INFO_FLAG_CLIENT_MODE;
		} else {
			local_wifi_info.flags |= WIFI_INFO_FLAG_CLIENT_MODE;
		}
		
		// Update the text areas with values for this mode
		if ((local_wifi_info.flags & WIFI_INFO_FLAG_CLIENT_MODE) != 0) {
			strcpy(local_wifi_info.ap_ssid, ssid_ta_str);
			strcpy(local_wifi_info.ap_pw, pw_ta_str);
			lv_ta_set_text(ta_ssid, local_wifi_info.sta_ssid);
			lv_ta_set_text(ta_pw, local_wifi_info.sta_pw);
			if (force_sta_pw_hidden) {
				lv_ta_set_pwd_mode(ta_pw, true);
			}
		} else {
			strcpy(local_wifi_info.sta_ssid, ssid_ta_str);
			strcpy(local_wifi_info.sta_pw, pw_ta_str);
			lv_ta_set_text(ta_ssid, local_wifi_info.ap_ssid);
			lv_ta_set_text(ta_pw, local_wifi_info.ap_pw);
		}
		set_active_text_area(SET_SSID);
		set_scan_icon();
		set_show_password_icon();
	}
}


/**
 * Set SSID text area handler - select the Set SSID text area if not already selected
 */
static void cb_ssid_ta(lv_obj_t* ta, lv_event_t event)
{
	if (selected_text_area_index != SET_SSID) {
		if (event == LV_EVENT_CLICKED) {
			// User just selected us
			set_active_text_area(SET_SSID);
		}
	}
}


/**
 * Set PW text area handler - select the Set PW text area if not already selected
 */
static void cb_pw_ta(lv_obj_t* ta, lv_event_t event)
{
	if (selected_text_area_index != SET_PW) {
		if (event == LV_EVENT_CLICKED) {
			// User just selected us
			set_active_text_area(SET_PW);
		}
	}
}


/**
 * Scan Wifi button callback
 */
static void cb_scan_wifi_btn(lv_obj_t* ta, lv_event_t event)
{
	if (event == LV_EVENT_CLICKED) {
		if (!wifi_scan_active) {
			setup_wifi_scan();
		} else {
			stop_wifi_scan();
		}
	}
}


/**
 * Show password button callback
 */
static void cb_show_pw_btn(lv_obj_t* lbl, lv_event_t event)
{
	if (event == LV_EVENT_CLICKED) {
		lv_ta_set_pwd_mode(ta_pw, !lv_ta_get_pwd_mode(ta_pw));
		set_show_password_icon();
		
		// Invalidate the object to force a redraw
		lv_obj_invalidate(ta_pw);
	}
}


/**
 * SSID List handler
 *   Copy selected item to the SSID text area
 */
static void cb_tbl_ssid_list(lv_obj_t* lbl, lv_event_t event)
{
	int num_rows;
	int row_h;
	int y_off;
	int row;
	lv_indev_t* touch;          // Input device
	lv_point_t cur_point;
	
	if (event == LV_EVENT_CLICKED) {
		touch = lv_indev_get_act();
		lv_indev_get_point(touch, &cur_point);
		
		// Finding the row is tricky since the table object may be scrolled.  We dig into
		// its object to find where it is relative to the screen and then figure which row
		// the click is in.
		num_rows = lv_table_get_row_cnt(tbl_ssid_list);
		if ((num_rows > 0) && ((local_wifi_info.flags & WIFI_INFO_FLAG_CLIENT_MODE) != 0)) {
			row_h = lv_obj_get_height(tbl_ssid_list) / num_rows;
			y_off = cur_point.y - tbl_ssid_list->coords.y1;
			if (y_off < 0) y_off = 0;
			row = y_off / row_h;
		
			// Copy the row text to SSID
			lv_ta_set_text(ta_ssid, lv_table_get_cell_value(tbl_ssid_list, row, 0));
		}
	}
}


/**
 * Keyboard handler
 *   Process close buttons ourselves
 *   Let the active text area process all other keypresses
 */
static void cb_kbd(lv_obj_t* kb, lv_event_t event)
{
	// First look for close keys
	if (event == LV_EVENT_CANCEL) {
		// Exit without changing anything
		gui_set_screen(GUI_SCREEN_SETTINGS);
	}
	
	if (event == LV_EVENT_APPLY) {
		char* ssid_ta_str = (char*) lv_ta_get_text(ta_ssid);
		char* pw_ta_str = (char*) lv_ta_get_text(ta_pw);
		
		if (strlen(ssid_ta_str) == 0) {
			gui_preset_message_box_string("SSID must contain a valid string", false);
			xTaskNotify(task_handle_gui, GUI_NOTIFY_MESSAGEBOX_MASK, eSetBits);
		} else if ((strlen(pw_ta_str) < 8) && (strlen(pw_ta_str) != 0)) {
			gui_preset_message_box_string("WPA2 passwords must be at least 8 characters", false);
			xTaskNotify(task_handle_gui, GUI_NOTIFY_MESSAGEBOX_MASK, eSetBits);
		} else {
			// Copy the text area strings to our data structure
			if ((local_wifi_info.flags & WIFI_INFO_FLAG_CLIENT_MODE) != 0) {
				strcpy(local_wifi_info.sta_ssid, ssid_ta_str);
				strcpy(local_wifi_info.sta_pw, pw_ta_str);
			} else {
				strcpy(local_wifi_info.ap_ssid, ssid_ta_str);
				strcpy(local_wifi_info.ap_pw, pw_ta_str);
			}
					
			// Then save the new WiFi configuration
			ps_set_wifi_info(&local_wifi_info);

			// Notify app_task of the update
			xTaskNotify(task_handle_app, APP_NOTIFY_NEW_WIFI_MASK, eSetBits);
			
			gui_set_screen(GUI_SCREEN_SETTINGS);
		}
	}
	
	// Then let the normal keyboard handler run (text handling in attached text area)
	lv_kb_def_event_cb(kb, event);
}


/**
 * LittlevGL task to monitor ongoing scan for completion
 */
static void task_wifi_scan(lv_task_t * task)
{
	int i;
	uint16_t num_ap_records;
	
	// Update our list once the scan is complete
	if (wifi_scan_is_complete()) {
		wifi_scan_active = false;
		num_ap_records = wifi_get_scan_records(&ap_infoP);
		
		// Hide the spinner
		lv_obj_set_hidden(pl_scanning, true);
		
		// Add the SSIDs to our list
		lv_table_set_row_cnt(tbl_ssid_list, num_ap_records);
		for (i=0; i<num_ap_records; i++) {
			lv_table_set_cell_value(tbl_ssid_list, i, 0, (const char *) ap_infoP->ssid);
			ap_infoP++;
		}
		
		// Restart WiFi if necessary (so user can exit without saving with no change to WiFi operation)
		if (!wifi_reinit()) {
			ESP_LOGE(TAG, "Could not restart WiFi after finishing scan");
		}
		
		// Finally end this task
		lv_task_del(scan_task);
		scan_task = NULL;
	}
}

