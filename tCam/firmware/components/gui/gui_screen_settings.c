/*
 * Settings GUI screen related functions, callbacks and event handlers
 *
 * Copyright 2020, 2023 Dan Julio
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
#include <math.h>
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gui_task.h"
#include "gui_screen_settings.h"
#include "gui_screen_browse.h"
#include "gui_screen_view.h"
#include "file_utilities.h"
#include "gui_utilities.h"
#include "lepton_utilities.h"
#include "power_utilities.h"
#include "ps_utilities.h"
#include "palettes.h"
#include "render.h"

//
// Settings GUI Screen constants
//

// Item numbers for Num Entry Screen
#define NUM_ENTRY_EMISSIVITY 1
#define NUM_ENTRY_RANGE_MIN  2
#define NUM_ENTRY_RANGE_MAX  3

// Recording intervals (string must match value array)
#define NUM_REC_INTERVALS    8
#define REC_INTERVAL_DD_STRING "Fastest\n500 mSec\n1 sec\n5 sec\n10 sec\n30 sec\n1 min\n5 min"

static const int rec_msec_intervals[] = {
	0, 500, 1000, 5000, 10000, 30000, 60000, 300000
};

// Gain modes
#define LEP_GAIN_DD_STRING "High\nLow\nAuto"



//
// Settings GUI Screen variables
//

//
// LVGL objects
//
static lv_obj_t* settings_screen;

// Header
static lv_obj_t* lbl_settings_title;
static lv_obj_t* btn_settings_exit;
static lv_obj_t* btn_settings_exit_label;

// Left Column Buttons for settings on other screens
static lv_obj_t* btn_wifi;
static lv_obj_t* btn_wifi_label;
static lv_obj_t* btn_network;
static lv_obj_t* btn_network_label;
static lv_obj_t* btn_clock;
static lv_obj_t* btn_clock_label;
static lv_obj_t* btn_storage;
static lv_obj_t* btn_storage_label;
static lv_obj_t* btn_info;
static lv_obj_t* btn_info_label;

// Row 1 Local controls
static lv_obj_t* lbl_dd_rec_interval;
static lv_obj_t* lbl_dd_gain;
static lv_obj_t* dd_rec_interval;
static lv_obj_t* dd_gain;

// Row 2 Local controls
static lv_obj_t* lbl_sw_man_range_mode;
static lv_obj_t* sw_man_range_mode;
static lv_obj_t* btn_min_range;
static lv_obj_t* btn_min_range_label;
static lv_obj_t* btn_max_range;
static lv_obj_t* btn_max_range_label;

// Row 3 Local controls
static lv_obj_t* lbl_btn_emissivity;
static lv_obj_t* btn_emissivity;
static lv_obj_t* btn_emissivity_label;
static lv_obj_t* btn_emissivity_lookup;
static lv_obj_t* btn_emissivity_lookup_label;
static lv_obj_t* lbl_sw_min_max;
static lv_obj_t* sw_min_max;

// Row 4 Local controls
static lv_obj_t* lbl_sw_metric_units_mode;
static lv_obj_t* lbl_sl_brightness;
static lv_obj_t* sw_metric_units_mode;
static lv_obj_t* sl_brightness;


// Flags to indicate a setting recorded in persistent storage has been updated
static bool gui_ps_val_updated;
static bool lep_ps_val_updated;



//
// Settings GUI Screen internal function forward declarations
//
static void initialize_screen_values();
static void update_emissivity();
static void update_man_range_items();
static void update_min_max();

static void cb_btn_exit(lv_obj_t * btn, lv_event_t event);
static void cb_btn_set_wifi(lv_obj_t * btn, lv_event_t event);
static void cb_btn_set_network(lv_obj_t * btn, lv_event_t event);
static void cb_btn_set_clock(lv_obj_t * btn, lv_event_t event);
static void cb_btn_set_storage(lv_obj_t * btn, lv_event_t event);
static void cb_btn_disp_info(lv_obj_t * btn, lv_event_t event);
static void cb_dd_gain(lv_obj_t * dd, lv_event_t event);
static void cb_dd_rec_interval(lv_obj_t * dd, lv_event_t event);
static void cb_sw_metric_units_mode(lv_obj_t * sw, lv_event_t event);
static void cb_btn_emissivity(lv_obj_t * sw, lv_event_t event);
static void cb_sw_min_max(lv_obj_t * sw, lv_event_t event);
static void cb_sw_range_mode(lv_obj_t * sw, lv_event_t event);
static void cb_btn_min_range(lv_obj_t * sw, lv_event_t event);
static void cb_btn_max_range(lv_obj_t * sw, lv_event_t event);
static void cb_btn_emissivity_lookup(lv_obj_t * btn, lv_event_t event);
static void cb_sl_brightness(lv_obj_t * sl, lv_event_t event);
static int intC_to_intF(int tc);



//
// Settings GUI Screen API
//

/**
 * Create the settings screen, its graphical objects and link necessary callbacks
 */
lv_obj_t* gui_screen_settings_create()
{
	settings_screen = lv_obj_create(NULL, NULL);
	lv_obj_set_size(settings_screen, LV_HOR_RES_MAX, LV_VER_RES_MAX);
	
	// Create the graphical elements for this screen
	//
	// Screen Title using a larger font (centered)
	lbl_settings_title = lv_label_create(settings_screen, NULL);
	static lv_style_t lbl_settings_title_style;
	lv_style_copy(&lbl_settings_title_style, gui_st.gui_theme->style.bg);
	lbl_settings_title_style.text.font = &lv_font_roboto_22;
	lv_label_set_style(lbl_settings_title, LV_LABEL_STYLE_MAIN, &lbl_settings_title_style);
	gui_print_static_centered_text(lbl_settings_title, S_TITLE_LBL_X, S_TITLE_LBL_Y, "Camera Settings");
	
	// Exit button
	btn_settings_exit = lv_btn_create(settings_screen, NULL);
	lv_obj_set_pos(btn_settings_exit, S_EXIT_BTN_X, S_EXIT_BTN_Y);
	lv_obj_set_size(btn_settings_exit, S_EXIT_BTN_W, S_EXIT_BTN_H);
	lv_obj_set_event_cb(btn_settings_exit, cb_btn_exit);
	btn_settings_exit_label = lv_label_create(btn_settings_exit, NULL);
	lv_label_set_static_text(btn_settings_exit_label, LV_SYMBOL_CLOSE);
	
	//
	// Left Column Buttons for settings on another screen
	//
	btn_wifi = lv_btn_create(settings_screen, NULL);
	lv_obj_set_pos(btn_wifi, BTN_EXT_SCREEN_X, SETTING_TOP);
	lv_obj_set_size(btn_wifi, BTN_EXT_WIDTH, BTN_EXT_HEIGHT);
	lv_obj_set_event_cb(btn_wifi, cb_btn_set_wifi);
	btn_wifi_label = lv_label_create(btn_wifi, NULL);
	lv_label_set_static_text(btn_wifi_label, "WIFI");
	
	btn_network = lv_btn_create(settings_screen, NULL);
	lv_obj_set_pos(btn_network, BTN_EXT_SCREEN_X, SETTING_TOP + BTN_EXT_DY);
	lv_obj_set_size(btn_network, BTN_EXT_WIDTH, BTN_EXT_HEIGHT);
	lv_obj_set_event_cb(btn_network, cb_btn_set_network);
	btn_network_label = lv_label_create(btn_network, NULL);
	lv_label_set_static_text(btn_network_label, "NETWORK");
	
	btn_clock = lv_btn_create(settings_screen, NULL);
	lv_obj_set_pos(btn_clock, BTN_EXT_SCREEN_X, SETTING_TOP + 2*BTN_EXT_DY);
	lv_obj_set_size(btn_clock, BTN_EXT_WIDTH, BTN_EXT_HEIGHT);
	lv_obj_set_event_cb(btn_clock, cb_btn_set_clock);
	btn_clock_label = lv_label_create(btn_clock, NULL);
	lv_label_set_static_text(btn_clock_label, "CLOCK");
	
	btn_storage = lv_btn_create(settings_screen, NULL);
	lv_obj_set_pos(btn_storage, BTN_EXT_SCREEN_X, SETTING_TOP + 3*BTN_EXT_DY);
	lv_obj_set_size(btn_storage, BTN_EXT_WIDTH, BTN_EXT_HEIGHT);
	lv_obj_set_event_cb(btn_storage, cb_btn_set_storage);
	btn_storage_label = lv_label_create(btn_storage, NULL);
	lv_label_set_static_text(btn_storage_label, "STORAGE");
	
	btn_info = lv_btn_create(settings_screen, NULL);
	lv_obj_set_pos(btn_info, BTN_EXT_SCREEN_X, SETTING_TOP + 4*BTN_EXT_DY);
	lv_obj_set_size(btn_info, BTN_EXT_WIDTH, BTN_EXT_HEIGHT);
	lv_obj_set_event_cb(btn_info, cb_btn_disp_info);
	btn_info_label = lv_label_create(btn_info, NULL);
	lv_label_set_static_text(btn_info_label, "INFO");
	
	//
	// Local controls
	//
	// Row 1 labels (Row 1 controls are below because we want drop-downs defined last
	// since they have to draw "over" other controls)
	lbl_dd_rec_interval = lv_label_create(settings_screen, NULL);
	lv_obj_set_pos(lbl_dd_rec_interval, LC_LBL_RI_X, LC_R1_LBL_Y);
	lv_label_set_static_text(lbl_dd_rec_interval, "Record Interval");
	
	lbl_dd_gain = lv_label_create(settings_screen, NULL);
	lv_obj_set_pos(lbl_dd_gain, LC_LBL_G_X, LC_R1_LBL_Y);
	lv_label_set_static_text(lbl_dd_gain, "Gain");
	
	// Row 2 labels
	lbl_sw_man_range_mode = lv_label_create(settings_screen, NULL);
	lv_obj_set_pos(lbl_sw_man_range_mode, LC_LBL_MR_X, LC_R2_LBL_Y);
	lv_label_set_static_text(lbl_sw_man_range_mode, "Man Range       Range Min        Range Max");
	
	// Row 2 controls
	sw_man_range_mode = lv_sw_create(settings_screen, NULL);
	lv_obj_set_pos(sw_man_range_mode, LC_SW_MR_X, LC_R2_CTRL_Y);
	lv_obj_set_size(sw_man_range_mode, LC_SW_MR_W, LC_SW_MR_H);
	lv_obj_set_event_cb(sw_man_range_mode, cb_sw_range_mode);
	
	btn_min_range = lv_btn_create(settings_screen, NULL);
	lv_obj_set_pos(btn_min_range, LC_BTN_MIN_X, LC_R2_CTRL_Y);
	lv_obj_set_size(btn_min_range, LC_BTN_MIN_W, LC_BTN_MIN_H);
	lv_obj_set_event_cb(btn_min_range, cb_btn_min_range);
	btn_min_range_label = lv_label_create(btn_min_range, NULL);
	
	btn_max_range = lv_btn_create(settings_screen, NULL);
	lv_obj_set_pos(btn_max_range, LC_BTN_MAX_X, LC_R2_CTRL_Y);
	lv_obj_set_size(btn_max_range, LC_BTN_MAX_W, LC_BTN_MAX_H);
	lv_obj_set_event_cb(btn_max_range, cb_btn_max_range);
	btn_max_range_label = lv_label_create(btn_max_range, NULL);
	
	// Row 3 labels
	lbl_btn_emissivity = lv_label_create(settings_screen, NULL);
	lv_obj_set_pos(lbl_btn_emissivity, LC_LBL_EM_X, LC_R3_LBL_Y);
	lv_label_set_static_text(lbl_btn_emissivity, "Emissivity");
	
	lbl_sw_min_max = lv_label_create(settings_screen, NULL);
	lv_obj_set_pos(lbl_sw_min_max, LC_LBL_MM_X, LC_R3_LBL_Y);
	lv_label_set_static_text(lbl_sw_min_max, "mM Marker");
	
	// Row 3 controls
	btn_emissivity = lv_btn_create(settings_screen, NULL);
	lv_obj_set_pos(btn_emissivity, LC_BTN_EM_X, LC_R3_CTRL_Y);
	lv_obj_set_size(btn_emissivity, LC_BTN_EM_W, LC_BTN_EM_H);
	lv_obj_set_event_cb(btn_emissivity, cb_btn_emissivity);
	btn_emissivity_label = lv_label_create(btn_emissivity, NULL);
	
	btn_emissivity_lookup = lv_btn_create(settings_screen, NULL);
	lv_obj_set_pos(btn_emissivity_lookup, LC_BTN_LU_X, LC_R3_CTRL_Y);
	lv_obj_set_size(btn_emissivity_lookup, LC_BTN_LU_W, LC_BTN_LU_H);
	lv_obj_set_event_cb(btn_emissivity_lookup, cb_btn_emissivity_lookup);
	btn_emissivity_lookup_label = lv_label_create(btn_emissivity_lookup, NULL);
	lv_label_set_static_text(btn_emissivity_lookup_label, "LOOKUP");
	
	sw_min_max = lv_sw_create(settings_screen, NULL);
	lv_obj_set_pos(sw_min_max, LC_SW_MM_X, LC_R3_CTRL_Y);
	lv_obj_set_size(sw_min_max, LC_SW_MM_W, LC_SW_MM_H);
	lv_obj_set_event_cb(sw_min_max, cb_sw_min_max);
	
	// Row 4 labels
	lbl_sw_metric_units_mode = lv_label_create(settings_screen, NULL);
	lv_obj_set_pos(lbl_sw_metric_units_mode, LC_LBL_MET_X, LC_R4_LBL_Y);
	lv_label_set_static_text(lbl_sw_metric_units_mode, "Metric");
	
	lbl_sl_brightness = lv_label_create(settings_screen, NULL);
	lv_obj_set_pos(lbl_sl_brightness, LC_LBL_BR_X, LC_R4_LBL_Y);
	lv_label_set_static_text(lbl_sl_brightness, "Display Brightness");
	
	// Row 4 controls
	sw_metric_units_mode = lv_sw_create(settings_screen, NULL);
	lv_obj_set_pos(sw_metric_units_mode, LC_SW_MET_X, LC_R4_CTRL_Y);
	lv_obj_set_size(sw_metric_units_mode, LC_SW_MET_W, LC_SW_MET_H);
	lv_obj_set_event_cb(sw_metric_units_mode, cb_sw_metric_units_mode);
	
	sl_brightness = lv_slider_create(settings_screen, NULL);
	lv_obj_set_pos(sl_brightness, LC_SL_BR_X, LC_R4_CTRL_Y);
	lv_obj_set_size(sl_brightness, LC_SL_BR_W, LC_SL_BR_H);
	lv_slider_set_range(sl_brightness, 10, 100);
	lv_obj_set_event_cb(sl_brightness, cb_sl_brightness);

	// Row 1 Drop-down palettes are last so they can correctly draw over controls below them
	dd_rec_interval = lv_ddlist_create(settings_screen, NULL);
	lv_obj_set_pos(dd_rec_interval, LC_DD_RI_X, LC_R1_CTRL_Y);
	lv_ddlist_set_fix_width(dd_rec_interval, LC_DD_RI_W);
	lv_ddlist_set_sb_mode(dd_rec_interval, LV_SB_MODE_AUTO);
	lv_ddlist_set_draw_arrow(dd_rec_interval, true);
	lv_obj_set_event_cb(dd_rec_interval, cb_dd_rec_interval);
	
	dd_gain = lv_ddlist_create(settings_screen, NULL);
	lv_obj_set_pos(dd_gain, LC_DD_G_X, LC_R1_CTRL_Y);
	lv_ddlist_set_fix_width(dd_gain, LC_DD_G_W);
	lv_ddlist_set_sb_mode(dd_gain, LV_SB_MODE_AUTO);
	lv_ddlist_set_draw_arrow(dd_gain, true);
	lv_obj_set_event_cb(dd_gain, cb_dd_gain);
	
	initialize_screen_values();
	
	return settings_screen;
}


/**
 * Tell this screen if it is newly active or not
 * (activating a screen (re)initializes its display)
 */
void gui_screen_settings_set_active(bool en)
{
	lep_config_t* lep_stP = lep_get_lep_st();
	
	if (en) {
		// Setup to detect a change that requires persistent storage to be updated on exit
		gui_ps_val_updated = false;
		lep_ps_val_updated = false;
		
		// See if we're active because we were returned to from a Num Entry screen
		if (num_entry_st.updated) {
			num_entry_st.updated = false;
			switch (num_entry_st.item_num) {
				case NUM_ENTRY_EMISSIVITY:
					if (lep_stP->emissivity != num_entry_st.return_val) {
						lep_stP->emissivity = num_entry_st.return_val;
						lepton_emissivity(lep_stP->emissivity);
						lep_ps_val_updated = true;
					}
					break;
				case NUM_ENTRY_RANGE_MIN:
					if (gui_st.man_range_min != gui_man_range_val_to_lep(num_entry_st.return_val)) {
						gui_st.man_range_min = gui_man_range_val_to_lep(num_entry_st.return_val);
						gui_ps_val_updated = true;
					}
					break;
				case NUM_ENTRY_RANGE_MAX:
					if (gui_st.man_range_max != gui_man_range_val_to_lep(num_entry_st.return_val)) {
						gui_st.man_range_max = gui_man_range_val_to_lep(num_entry_st.return_val);
						gui_ps_val_updated = true;
					}
					break;
			}
		}
		
		// Disable non-radiometric controls if we don't have a radiometric camera
		lv_obj_set_hidden(lbl_sw_man_range_mode , !gui_st.is_radiometric);
		lv_obj_set_hidden(sw_man_range_mode, !gui_st.is_radiometric);
		lv_obj_set_hidden(btn_min_range, !gui_st.is_radiometric);
		lv_obj_set_hidden(btn_max_range, !gui_st.is_radiometric);
		lv_obj_set_hidden(lbl_btn_emissivity, !gui_st.is_radiometric);
		lv_obj_set_hidden(btn_emissivity, !gui_st.is_radiometric);
		lv_obj_set_hidden(btn_emissivity_lookup, !gui_st.is_radiometric);
		lv_obj_set_hidden(lbl_dd_gain , !gui_st.is_radiometric);
		lv_obj_set_hidden(dd_gain, !gui_st.is_radiometric);
		lv_obj_set_hidden(sw_metric_units_mode, !gui_st.is_radiometric);
		lv_obj_set_hidden(lbl_sw_metric_units_mode, !gui_st.is_radiometric);
		
		// Update graphics
		lv_ddlist_set_selected(dd_gain, lep_stP->gain_mode);
	
		gui_st.temp_unit_C ? lv_sw_on(sw_metric_units_mode, LV_ANIM_OFF) : lv_sw_off(sw_metric_units_mode, LV_ANIM_OFF);
	
		update_emissivity();
		update_man_range_items();
		update_min_max();
		
		lv_slider_set_value(sl_brightness, gui_st.lcd_brightness, LV_ANIM_OFF);
	}
}


//
// Main GUI Screen internal functions
//

static void initialize_screen_values()
{
	int i;
	lep_config_t* lep_stP = lep_get_lep_st();
	
	// Get the index of the currently selected recording interval
	for (i=0; i<NUM_REC_INTERVALS; i++) {
		if (rec_msec_intervals[i] == gui_st.recording_interval) {
			break;
		}
	}
	if (i == NUM_REC_INTERVALS) {
		// Didn't find a recording interval we know about so reset
		i = 0;
		gui_ps_val_updated = true;
	}
	
	// Initialize displayed on-screen values
	lv_ddlist_set_options(dd_gain, LEP_GAIN_DD_STRING);
	lv_ddlist_set_selected(dd_gain, lep_stP->gain_mode);
	lv_ddlist_set_options(dd_rec_interval, REC_INTERVAL_DD_STRING);
	lv_ddlist_set_selected(dd_rec_interval, i);
	
	gui_st.temp_unit_C ? lv_sw_on(sw_metric_units_mode, LV_ANIM_OFF) : lv_sw_off(sw_metric_units_mode, LV_ANIM_OFF);
	
	update_emissivity();
	update_man_range_items();
}


static void update_emissivity()
{
	static char buf[4];   // Statically allocated for lv_label_set_static_text
	int e;
	lep_config_t* lep_stP = lep_get_lep_st();
	
	e = lep_stP->emissivity;
	if (e < EMISSIVITY_BTN_MIN) e = EMISSIVITY_BTN_MIN;
	if (e > EMISSIVITY_BTN_MAX) e = EMISSIVITY_BTN_MAX;
	
	sprintf(buf, "%d", e);
	lv_label_set_static_text(btn_emissivity_label, buf);
	lv_obj_invalidate(btn_emissivity_label);
}


static void update_man_range_items()
{
	static char min_buf[6];   // Statically allocated for lv_label_set_static_text
	static char max_buf[6];   // Statically allocated for lv_label_set_static_text
	float tf;
	int ti;
	
	// Manual Range Switch
	gui_st.man_range_mode ? lv_sw_on(sw_man_range_mode, LV_ANIM_OFF) : lv_sw_off(sw_man_range_mode, LV_ANIM_OFF);
	
	// Manual Range Minimum value
	tf = gui_range_val_to_disp_temp(gui_st.man_range_min);
	ti = (int) round(tf);
	sprintf(min_buf, "%d", ti);
	lv_label_set_static_text(btn_min_range_label, min_buf);
	lv_obj_invalidate(btn_min_range_label);
	
	// Manual Range Maximum value
	tf = gui_range_val_to_disp_temp(gui_st.man_range_max);
	ti = (int) round(tf);
	sprintf(max_buf, "%d", ti);
	lv_label_set_static_text(btn_max_range_label, max_buf);
	lv_obj_invalidate(btn_max_range_label);
}


static void update_min_max()
{
	gui_st.min_max_enable ? lv_sw_on(sw_min_max, LV_ANIM_OFF) : lv_sw_off(sw_min_max, LV_ANIM_OFF);
}


static void cb_btn_exit(lv_obj_t * btn, lv_event_t event)
{
	lep_config_t* lep_stP = lep_get_lep_st();
	
	if (event == LV_EVENT_CLICKED) {
		if (gui_ps_val_updated) {
			// Update persistent storage on exit if necessary
			ps_set_gui_state(&gui_st);
		}
		if (lep_ps_val_updated) {
			// Update lepton persistent storage on exit if necessary
			ps_set_lep_state(lep_stP);
		}
		gui_set_screen(GUI_SCREEN_MAIN);
	}
}


static void cb_btn_set_wifi(lv_obj_t * btn, lv_event_t event)
{
	if (event == LV_EVENT_CLICKED) {
		gui_set_screen(GUI_SCREEN_WIFI);
	}
}


static void cb_btn_set_network(lv_obj_t * btn, lv_event_t event)
{
	if (event == LV_EVENT_CLICKED) {
		gui_set_screen(GUI_SCREEN_NETWORK);
	}
}


static void cb_btn_set_clock(lv_obj_t * btn, lv_event_t event)
{
	if (event == LV_EVENT_CLICKED) {
		gui_set_screen(GUI_SCREEN_TIME);
	}
}


static void cb_btn_set_storage(lv_obj_t * btn, lv_event_t event)
{
	if (event == LV_EVENT_CLICKED) {
		gui_screen_view_state_init();  // Set VIEW state from main state in case user goes to VIEW
		gui_set_screen(GUI_SCREEN_BROWSE);
	}
}


static void cb_btn_disp_info(lv_obj_t * btn, lv_event_t event)
{
	if (event == LV_EVENT_CLICKED) {
		gui_set_screen(GUI_SCREEN_INFO);
	}
}


static void cb_dd_gain(lv_obj_t * dd, lv_event_t event)
{
	int new_sel;
	lep_config_t* lep_stP = lep_get_lep_st();
	
	if (event == LV_EVENT_VALUE_CHANGED) {
		new_sel = lv_ddlist_get_selected(dd);
		if (new_sel != lep_stP->gain_mode) {
			lep_stP->gain_mode = new_sel;
			lepton_gain_mode(new_sel);
			lep_ps_val_updated = true;
		}
	}
}


static void cb_dd_rec_interval(lv_obj_t * dd, lv_event_t event)
{
	int new_sel;
	
	if (event == LV_EVENT_VALUE_CHANGED) {
		new_sel = lv_ddlist_get_selected(dd);
		if (rec_msec_intervals[new_sel] != gui_st.recording_interval) {
			gui_st.recording_interval = rec_msec_intervals[new_sel];
			gui_ps_val_updated = true;
		}
	}
}


static void cb_sw_metric_units_mode(lv_obj_t * sw, lv_event_t event)
{
	if (event == LV_EVENT_VALUE_CHANGED) {
		gui_st.temp_unit_C = lv_sw_get_state(sw);
		
		// Update controls on this page that display in units
		update_man_range_items();
		
		gui_ps_val_updated = true;
	}
}


static void cb_btn_emissivity(lv_obj_t * sw, lv_event_t event)
{
	lep_config_t* lep_stP = lep_get_lep_st();
	
	if (event == LV_EVENT_CLICKED) {
		// Setup for Number Entry screen
		strcpy(num_entry_st.item_name, "Emissivity");
		sprintf(num_entry_st.description, "Percent %d - %d", EMISSIVITY_BTN_MIN, EMISSIVITY_BTN_MAX);          
		num_entry_st.item_num = NUM_ENTRY_EMISSIVITY;
		num_entry_st.initial_val = lep_stP->emissivity;
		num_entry_st.min_val = EMISSIVITY_BTN_MIN;
		num_entry_st.max_val = EMISSIVITY_BTN_MAX;
		num_entry_st.calling_screen = GUI_SCREEN_SETTINGS;
		
		gui_set_screen(GUI_SCREEN_NUM_ENTRY);
	}
}


static void cb_sw_min_max(lv_obj_t * sw, lv_event_t event)
{
	if (event == LV_EVENT_VALUE_CHANGED) {
		gui_st.min_max_enable = lv_sw_get_state(sw);
		gui_ps_val_updated = true;
	}
}


static void cb_sw_range_mode(lv_obj_t * sw, lv_event_t event)
{
	if (event == LV_EVENT_VALUE_CHANGED) {
		gui_st.man_range_mode = lv_sw_get_state(sw);
		gui_ps_val_updated = true;
	}
}


static void cb_btn_min_range(lv_obj_t * sw, lv_event_t event)
{
	float f;
	int val, min, max;
	
	if (event == LV_EVENT_CLICKED) {
		// Legal Range
		f = gui_range_val_to_disp_temp(gui_st.man_range_min);
		val = (int) round(f);
		min = gui_st.temp_unit_C ? RANGE_BTN_MIN : intC_to_intF(RANGE_BTN_MIN);
		f = gui_range_val_to_disp_temp(gui_st.man_range_max);
		max = (int) round(f) - 1;
	
		// Setup for Number Entry screen
		strcpy(num_entry_st.item_name, "Range Min");
		sprintf(num_entry_st.description, "Temperature %d  -  %d", min, max);          
		num_entry_st.item_num = NUM_ENTRY_RANGE_MIN;
		num_entry_st.initial_val = val;
		num_entry_st.min_val = min;
		num_entry_st.max_val = max;
		num_entry_st.calling_screen = GUI_SCREEN_SETTINGS;
		
		gui_set_screen(GUI_SCREEN_NUM_ENTRY);
	}
}


static void cb_btn_max_range(lv_obj_t * sw, lv_event_t event)
{
	float f;
	int val, min, max;
	
	if (event == LV_EVENT_CLICKED) {
		// Legal Range
		f = gui_range_val_to_disp_temp(gui_st.man_range_max);
		val = (int) round(f);
		f = gui_range_val_to_disp_temp(gui_st.man_range_min);
		min = (int) round(f) + 1;
		max = gui_st.temp_unit_C ? RANGE_BTN_MAX : intC_to_intF(RANGE_BTN_MAX);
	
		// Setup for Number Entry screen
		strcpy(num_entry_st.item_name, "Range Max");
		sprintf(num_entry_st.description, "Temperature %d  -  %d", min, max);          
		num_entry_st.item_num = NUM_ENTRY_RANGE_MAX;
		num_entry_st.initial_val = val;
		num_entry_st.min_val = min;
		num_entry_st.max_val = max;
		num_entry_st.calling_screen = GUI_SCREEN_SETTINGS;
		
		gui_set_screen(GUI_SCREEN_NUM_ENTRY);
	}
}


static void cb_btn_emissivity_lookup(lv_obj_t * btn, lv_event_t event)
{
	if (event == LV_EVENT_CLICKED) {
		gui_set_screen(GUI_SCREEN_EMISSIVITY);
	}
}


static void cb_sl_brightness(lv_obj_t * sl, lv_event_t event)
{
	int16_t new_val;
	
	if (event == LV_EVENT_VALUE_CHANGED) {
		new_val = lv_slider_get_value(sl);
		
		if ((uint8_t) new_val != gui_st.lcd_brightness) {
			gui_st.lcd_brightness = (uint8_t) new_val;
			power_set_brightness(gui_st.lcd_brightness);
			gui_ps_val_updated = true;
		}
	}
}


static int intC_to_intF(int tc)
{
	float t;
	
	t = tc * 1.8 + 32;
	
	return (int) round(t);
}
