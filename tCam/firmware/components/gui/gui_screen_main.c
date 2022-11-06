/*
 * Main GUI screen related functions, callbacks and event handlers
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
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gui_screen_main.h"
#include "gui_screen_view.h"
#include "app_task.h"
#include "cmd_task.h"
#include "file_task.h"
#include "gui_task.h"
#include "file_utilities.h"
#include "gui_utilities.h"
#include "lepton_utilities.h"
#include "power_utilities.h"
#include "ps_utilities.h"
#include "sys_utilities.h"
#include "time_utilities.h"
#include "lv_conf.h"
#include "palettes.h"
#include "system_config.h"
#include "render.h"
#include <stdio.h>
#include <string.h>



//
// Main GUI Screen constants
//
#define MAX_INFO_MSG_LEN 32


//
// Main GUI Screen variables
//

//
// LVGL objects
//
static lv_obj_t* main_screen;

// Header
static lv_obj_t* lbl_ssid;
static lv_obj_t* lbl_time;
static lv_obj_t* lbl_batt_status;
static lv_obj_t* lbl_sdcard_status;

// Colormap
static lv_obj_t* canvas_colormap;
static lv_style_t colormap_entry_style;
static lv_style_t colormap_temp_style;
static lv_style_t colormap_clear_temp_style;
static lv_style_t colormap_marker_style;

// Lepton Image
static lv_img_dsc_t lepton_img_dsc;
static lv_obj_t* img_lepton;

// Record indicator label
static lv_obj_t* lbl_recording;

// Controls
static lv_obj_t* sw_record_mode;
static lv_obj_t* sw_record_mode_label;
static lv_obj_t* btn_browse;
static lv_obj_t* btn_browse_label;
static lv_obj_t* btn_setup;
static lv_obj_t* btn_setup_label;
static lv_obj_t* btn_ffc;
static lv_obj_t* btn_ffc_label;
static lv_obj_t* btn_range_mode;
static lv_obj_t* btn_range_mode_label;
static lv_obj_t* btn_agc;
static lv_obj_t* btn_agc_label;
static lv_obj_t* btn_poweroff;
static lv_obj_t* btn_poweroff_label;

// Bottom information area
static lv_obj_t* lbl_spot_temp;
static lv_obj_t* lbl_info_message;
static lv_task_t* lbl_info_message_task;

// Status update task
static lv_task_t* task_update;


// Statically allocated info message buffer
static char info_message[MAX_INFO_MSG_LEN];

// Screen state
static bool first_lepton_update;  // Set to update controls on first lepton telemetry
static bool agc_updated;          // Set when necessary to extract lepton agc_enabled state 
								  // from telemetry to update controls that depend on it
static uint16_t cur_lep_min_val;  // Set from current lep_gui_buffer
static uint16_t cur_lep_max_val;



//
// Main GUI Screen internal function forward declarations
//
static void initialize_screen_values();
static void set_colormap(int n);
static void set_spot_marker(int offset, bool erase);

static void status_update_task(lv_task_t * task);
static void update_batt();
static void update_sdcard();
static void update_time();
static void update_wifi();
static void update_recording_label();
static void update_colormap();
static void update_colormap_temps(lep_buffer_t* sys_bufP, bool init);
static void update_colormap_marker(lep_buffer_t* sys_bufP);
static void update_spot_temp(lep_buffer_t* sys_bufP, bool init);
static void update_agc_btn_label();
static void update_range_mode_btn_label();

static void cb_lepton_image(lv_obj_t * img, lv_event_t event);
static void cb_sw_record_mode(lv_obj_t * sw, lv_event_t event);
static void cb_btn_browse(lv_obj_t * btn, lv_event_t event);
static void cb_btn_setup(lv_obj_t * btn, lv_event_t event);
static void cb_btn_ffc(lv_obj_t * btn, lv_event_t event);
static void cb_btn_agc(lv_obj_t * btn, lv_event_t event);
static void cb_btn_poweroff(lv_obj_t * btn, lv_event_t event);
static void cb_btn_range_mode(lv_obj_t * btn, lv_event_t event);
static void cb_spot_temp(lv_obj_t * btn, lv_event_t event);
static void cb_colormap_canvas(lv_obj_t * btn, lv_event_t event);

static void task_info_message_timeout(lv_task_t * task);



//
// Main GUI Screen API
//

/**
 * Create the main screen, its graphical objects and link necessary callbacks
 */
lv_obj_t* gui_screen_main_create()
{
	main_screen = lv_obj_create(NULL, NULL);
	lv_obj_set_size(main_screen, LV_HOR_RES_MAX, LV_VER_RES_MAX);
	
	// Create the graphical elements for this screen
	//
	// Header
	lbl_ssid = lv_label_create(main_screen, NULL);
	lv_obj_set_pos(lbl_ssid, MAIN_SSID_LBL_X, MAIN_SSID_LBL_Y);
	lv_obj_set_width(lbl_ssid, MAIN_SSID_LBL_W);
	lv_label_set_recolor(lbl_ssid, true);
	
	lbl_time = lv_label_create(main_screen, NULL);
	lv_obj_set_pos(lbl_time, MAIN_TIME_LBL_X, MAIN_TIME_LBL_Y);
	lv_obj_set_width(lbl_time, MAIN_TIME_LBL_W);
	
	lbl_batt_status = lv_label_create(main_screen, NULL);
	lv_obj_set_pos(lbl_batt_status, MAIN_BATT_LBL_X, MAIN_BATT_LBL_Y);
	lv_obj_set_width(lbl_batt_status, MAIN_BATT_LBL_W);
	lv_label_set_recolor(lbl_batt_status, true);
	
	lbl_sdcard_status = lv_label_create(main_screen, NULL);
	lv_obj_set_pos(lbl_sdcard_status, MAIN_SDCARD_LBL_X, MAIN_SDCARD_LBL_Y);
	lv_obj_set_width(lbl_sdcard_status, MAIN_SDCARD_LBL_W);
	
	// Lepton image Color map area
	canvas_colormap = lv_canvas_create(main_screen, NULL);
	lv_canvas_set_buffer(canvas_colormap, gui_cmap_canvas_buffer, MAIN_CMAP_CANVAS_WIDTH, MAIN_CMAP_CANVAS_HEIGHT, LV_IMG_CF_TRUE_COLOR);
	lv_obj_set_pos(canvas_colormap, MAIN_CMAP_X, MAIN_CMAP_Y);
	lv_obj_set_click(canvas_colormap, true);
	lv_obj_set_event_cb(canvas_colormap, cb_colormap_canvas);
	
	lv_style_copy(&colormap_entry_style, &lv_style_plain);
	colormap_entry_style.line.width = 1;
	colormap_entry_style.line.opa = LV_OPA_COVER;
	
	lv_style_copy(&colormap_temp_style, gui_st.gui_theme->style.bg);
	
	lv_style_copy(&colormap_clear_temp_style, &lv_style_plain);
	colormap_clear_temp_style.body.main_color = MAIN_PALETTE_BG_COLOR;
	colormap_clear_temp_style.body.grad_color = MAIN_PALETTE_BG_COLOR;
	
	lv_style_copy(&colormap_marker_style, gui_st.gui_theme->style.bg);
	
	// Lepton image data structure
	lepton_img_dsc.header.always_zero = 0;
	lepton_img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
	lepton_img_dsc.header.w = LEP_IMG_WIDTH;
	lepton_img_dsc.header.h = LEP_IMG_HEIGHT;
	lepton_img_dsc.data_size = LEP_IMG_WIDTH * LEP_IMG_HEIGHT * 2;
	lepton_img_dsc.data = (uint8_t*) gui_lep_canvas_buffer;

	// Lepton Image Area
	img_lepton = lv_img_create(main_screen, NULL);
	lv_img_set_src(img_lepton, &lepton_img_dsc);
	lv_obj_set_pos(img_lepton, MAIN_IMG_X, MAIN_IMG_Y);
	lv_obj_set_click(img_lepton, true);
	lv_obj_set_event_cb(img_lepton, cb_lepton_image);
	
	// Recording indicator label area
	lbl_recording = lv_label_create(main_screen, NULL);
	lv_obj_set_pos(lbl_recording, MAIN_REC_LBL_X, MAIN_REC_LBL_Y);
	static lv_style_t lbl_recording_style;
	lv_style_copy(&lbl_recording_style, gui_st.gui_theme->style.bg);
	lbl_recording_style.text.font = &lv_font_roboto_22;
	lbl_recording_style.text.color = LV_COLOR_RED;
	lv_label_set_style(lbl_recording, LV_LABEL_STYLE_MAIN, &lbl_recording_style);
	lv_label_set_static_text(lbl_recording, "REC");
	
	// Button Area
	sw_record_mode = lv_sw_create(main_screen, NULL);
	lv_obj_set_pos(sw_record_mode, MAIN_MODE_SW_X, MAIN_MODE_SW_Y);
	lv_obj_set_size(sw_record_mode, MAIN_MODE_SW_W, MAIN_MODE_SW_H);
	lv_obj_set_event_cb(sw_record_mode, cb_sw_record_mode);
	
	sw_record_mode_label = lv_label_create(main_screen, NULL);
	lv_obj_set_pos(sw_record_mode_label, MAIN_MODE_SW_LBL_X, MAIN_MODE_SW_LBL_Y);
	lv_label_set_static_text(sw_record_mode_label, LV_SYMBOL_IMAGE "        " LV_SYMBOL_VIDEO);
	
	btn_browse = lv_btn_create(main_screen, NULL);
	lv_obj_set_pos(btn_browse, MAIN_BROWSE_BTN_X, MAIN_BROWSE_BTN_Y);
	lv_obj_set_size(btn_browse, MAIN_BROWSE_BTN_W, MAIN_BROWSE_BTN_H);
	lv_obj_set_event_cb(btn_browse, cb_btn_browse);
	btn_browse_label = lv_label_create(btn_browse, NULL);
	lv_label_set_recolor(btn_browse_label, true);
	
	btn_setup = lv_btn_create(main_screen, NULL);
	lv_obj_set_pos(btn_setup, MAIN_SETUP_BTN_X, MAIN_SETUP_BTN_Y);
	lv_obj_set_size(btn_setup, MAIN_SETUP_BTN_W, MAIN_SETUP_BTN_H);
	lv_obj_set_event_cb(btn_setup, cb_btn_setup);
	btn_setup_label = lv_label_create(btn_setup, NULL);
	lv_label_set_static_text(btn_setup_label, LV_SYMBOL_SETTINGS);
	
	btn_poweroff = lv_btn_create(main_screen, NULL);
	lv_obj_set_pos(btn_poweroff, MAIN_PWROFF_BTN_X, MAIN_PWROFF_BTN_Y);
	lv_obj_set_size(btn_poweroff, MAIN_PWROFF_BTN_W, MAIN_PWROFF_BTN_H);
	lv_obj_set_event_cb(btn_poweroff, cb_btn_poweroff);
	btn_poweroff_label = lv_label_create(btn_poweroff, NULL);
	lv_label_set_static_text(btn_poweroff_label, LV_SYMBOL_POWER);
	
	btn_ffc = lv_btn_create(main_screen, NULL);
	lv_obj_set_pos(btn_ffc, MAIN_FFC_BTN_X, MAIN_FFC_BTN_Y);
	lv_obj_set_size(btn_ffc, MAIN_FFC_BTN_W, MAIN_FFC_BTN_H);
	lv_obj_set_event_cb(btn_ffc, cb_btn_ffc);
	btn_ffc_label = lv_label_create(btn_ffc, NULL);
	lv_label_set_static_text(btn_ffc_label, "FFC");
	
	btn_agc = lv_btn_create(main_screen, NULL);
	lv_obj_set_pos(btn_agc, MAIN_AGC_BTN_X, MAIN_AGC_BTN_Y);
	lv_obj_set_size(btn_agc, MAIN_AGC_BTN_W, MAIN_AGC_BTN_H);
	lv_obj_set_event_cb(btn_agc, cb_btn_agc);
	btn_agc_label = lv_label_create(btn_agc, NULL);
	
	btn_range_mode = lv_btn_create(main_screen, NULL);
	lv_obj_set_pos(btn_range_mode, MAIN_RNGMODE_BTN_X, MAIN_RNGMODE_BTN_Y);
	lv_obj_set_size(btn_range_mode, MAIN_RNGMODE_BTN_W, MAIN_RNGMODE_BTN_H);
	lv_obj_set_event_cb(btn_range_mode, cb_btn_range_mode);
	btn_range_mode_label = lv_label_create(btn_range_mode, NULL);
	lv_label_set_recolor(btn_range_mode_label, true);

	// Spot Temp (will be centered)
	lbl_spot_temp = lv_label_create(main_screen, NULL);
	lv_obj_set_click(lbl_spot_temp, true);
	lv_obj_set_event_cb(lbl_spot_temp, cb_spot_temp);
	
	// Modify lbl_spot_temp to use a larger font
	static lv_style_t lbl_spot_temp_style;
	lv_style_copy(&lbl_spot_temp_style, gui_st.gui_theme->style.bg);
	lbl_spot_temp_style.text.font = &lv_font_roboto_22;
	lv_label_set_style(lbl_spot_temp, LV_LABEL_STYLE_MAIN, &lbl_spot_temp_style);
	
	// Bottom Information Area
	lbl_info_message = lv_label_create(main_screen, NULL);
	lv_obj_set_pos(lbl_info_message, MAIN_INFO_LBL_X, MAIN_INFO_LBL_Y);
	
	// Force immediate update of some regions
	initialize_screen_values();
	
	return main_screen;
}


/**
 * Tell this screen if it is newly active or not
 * (activating a screen (re)initializes its display)
 */
void gui_screen_main_set_active(bool en)
{
	uint16_t* ptr;
	
	if (en) {
		// Update items that may have changed but won't normally be updated immediately
		// by the lepton image stream
		update_wifi();
		update_batt();
		update_sdcard();
		update_time();
		update_agc_btn_label();
		update_range_mode_btn_label();
		
		// Clear the image area (to color black)
		ptr = gui_lep_canvas_buffer;
		while (ptr < (gui_lep_canvas_buffer + LEP_IMG_PIXELS)) {
			*ptr++ = 0;
		}
		
		// Draw the initial palette
		lv_canvas_fill_bg(canvas_colormap, MAIN_PALETTE_BG_COLOR);
		set_palette(gui_st.palette);
		update_colormap();
		update_colormap_temps(NULL, true);
		
		// Flag for lepton datastream processing
		first_lepton_update = true;
		agc_updated = false;
		
		// Status line update sub-task runs twice per second
		task_update = lv_task_create(status_update_task, 500, LV_TASK_PRIO_LOW, NULL);
	} else {
		if (task_update != NULL) {
			lv_task_del(task_update);
			task_update = NULL;
		}
	}
}


/**
 * Tell this screen about updates due to a picture being taken or recording enabled
 * or disabled.
 */
void gui_screen_main_set_image_state(int st)
{
	switch (st) {
		case IMG_STATE_PICTURE:
			if (gui_st.record_mode) {
				// Set switch to image mode
				lv_sw_off(sw_record_mode, LV_ANIM_ON);
				
				// Update visual state
				gui_st.record_mode = false;
				update_recording_label();
			}
			break;
		
		case IMG_STATE_REC_ON:
			if (!gui_st.record_mode) {
				// Set switch to record position
				gui_st.record_mode = true;
				lv_sw_on(sw_record_mode, LV_ANIM_ON);
			}
			
			// Update visual state
			gui_st.recording = true;
			update_recording_label();
			break;
		
		case IMG_STATE_REC_OFF:
			gui_st.recording = false;
			update_recording_label();
			break;
	}
}


/**
 * Update the lepton display.
 */
void gui_screen_main_update_lep_image(int n)
{
	lep_config_t* lep_stP = lep_get_lep_st();
	
	// Lock the buffer
	xSemaphoreTake(lep_gui_buffer[n].mutex, portMAX_DELAY);
	
	// Get state from the telemetry with this image
	gui_st.agc_enabled = (lepton_get_tel_status(lep_gui_buffer[n].lep_telemP) & LEP_STATUS_AGC_STATE) == LEP_STATUS_AGC_STATE;
	gui_st.rad_high_res = lep_gui_buffer[n].lep_telemP[LEP_TEL_TLIN_RES] != 0;
	if (!lep_info.valid) {
		// Get static information from the Lepton just once
		lep_info.valid = true;
		for (int i=0; i<4; i++) lep_info.lep_version[i] = lep_gui_buffer[n].lep_telemP[LEP_TEL_REV_0 + i];
		gui_st.is_radiometric = gui_tel_is_radiometric(lep_gui_buffer[n].lep_telemP);
		
		// Turn on the range button for radiometric cameras
		if (gui_st.is_radiometric) {
			lv_obj_set_click(btn_range_mode, true);
		}
	}
	lep_info.lep_fpa_temp_k100 = lep_gui_buffer[n].lep_telemP[LEP_TEL_FPA_T_K100];
	lep_info.lep_housing_temp_k100 = lep_gui_buffer[n].lep_telemP[LEP_TEL_HSE_T_K100];
		
	// Update image
	render_lep_data(&lep_gui_buffer[n], gui_lep_canvas_buffer, &gui_st);
	
	// Update spot meter on image
	if (gui_st.spotmeter_enable && gui_st.is_radiometric) {
		render_spotmeter(&lep_gui_buffer[n], gui_lep_canvas_buffer);
	}
	lepton_img_dsc.data = (uint8_t*) gui_lep_canvas_buffer;
	
	// Update temps
	update_colormap_temps(&lep_gui_buffer[n], false);
	update_colormap_marker(&lep_gui_buffer[n]);
	update_spot_temp(&lep_gui_buffer[n], false);
	
	// Record this buffer's min/max
	cur_lep_min_val = lep_gui_buffer[n].lep_min_val;
	cur_lep_max_val = lep_gui_buffer[n].lep_max_val;
	
	// Unlock
	xSemaphoreGive(lep_gui_buffer[n].mutex);
		
	// Update items that require current screen-entry state from telemetry
	if (first_lepton_update || (agc_updated && (gui_st.agc_enabled == lep_stP->agc_set_enabled))) {
		first_lepton_update = false;
		agc_updated = false;
		
		update_agc_btn_label();
		update_range_mode_btn_label();   // We need agc_enabled to handle case where AGC changed
	}
	
	// Finally invalidate the object to force it to redraw from the buffer
	lv_obj_invalidate(img_lepton);
}


/**
 * Update the display message area.  Set disp_secs to 0 for a untimed message.
 */
void gui_screen_display_message(char * msg, int disp_secs)
{
	int i = 0;
	
	// Copy the message and display it
	while ((i < MAX_INFO_MSG_LEN-1) && (*msg != 0)) {
		info_message[i++] = *msg++;
	}
	info_message[i] = 0;
	lv_label_set_static_text(lbl_info_message, info_message);
	
	// Delete an already running timer task
	if (lbl_info_message_task != NULL) {
		lv_task_del(lbl_info_message_task);
		lbl_info_message_task = NULL;
	}
	
	// Start a timed task if necessary
	if (disp_secs != 0) {
		// Create and start a one-shot task to take the message down
		lbl_info_message_task = lv_task_create(task_info_message_timeout, 1000 * disp_secs, LV_TASK_PRIO_LOW, NULL);
		lv_task_once(lbl_info_message_task);
	}
}


/**
 * Allows an external source to note it modified the AGC 
 */
void gui_screen_agc_updated()
{
	// Set flag to force update of controls when telemetry state indicates change has occurred
	agc_updated = true;
}



//
// Main GUI Screen internal functions
//

static void initialize_screen_values()
{
	// Force an initial update of misc on-screen items so LVGL defaults won't show
	update_wifi();
	update_batt();
	update_sdcard();
	update_time();
	update_recording_label();
	update_spot_temp(&lep_gui_buffer[0], true);  // Force blank of text area
	update_agc_btn_label();
	update_range_mode_btn_label();
	
	// Blank info message
	lbl_info_message_task = NULL;
	info_message[0] = 0;
	lv_label_set_static_text(lbl_info_message, info_message);
	
	// Range button starts off disabled until we get the first image so we can determine
	// what kind of camera is attached
	lv_obj_set_click(btn_range_mode, false);
	
	// No task yet
	task_update = NULL;
}


static void set_colormap(int n)
{
	set_palette(n);
	gui_screen_display_message(get_palette_name(n), 1);
	update_colormap();
}


static void set_spot_marker(int offset, bool erase)
{
	lv_point_t points[3];
	int y = offset + (MAIN_CMAP_CANVAS_HEIGHT - 256)/2;
	
	if (erase) {
		// Erase a rectangular region surrounding the drawn marker
		lv_canvas_draw_rect(canvas_colormap, MAIN_CMAP_MARKER_X1-1, y-(MAIN_CMAP_MARKER_H/2)-1,
		                    MAIN_CMAP_MARKER_X2 - MAIN_CMAP_MARKER_X1 + 2,
		                    MAIN_CMAP_MARKER_H + 2, &colormap_clear_temp_style);
	} else {
		// Draw the marker
		points[0].x = MAIN_CMAP_MARKER_X1;
		points[0].y = y;
		points[1].x = MAIN_CMAP_MARKER_X2;
		points[1].y = y-3;
		points[2].x = MAIN_CMAP_MARKER_X2;
		points[2].y = y+3;
		colormap_entry_style.body.main_color = LV_COLOR_WHITE;
		lv_canvas_draw_polygon(canvas_colormap, points, 3, &colormap_entry_style);
	}
}


void status_update_task(lv_task_t * task)
{
	update_wifi();
	update_batt();
	update_sdcard();
	update_time();
}


static void update_batt()
{
	static char batt_buf[17];  // Statically allocated for lv_label_set_static_text
	static batt_status_t prev_bs = {0, 0, 0, 0, BATT_CRIT, CHARGE_FAULT};
	batt_status_t bs;
	
	// Get current values
	power_get_batt(&bs);
	
	if ((bs.batt_state != prev_bs.batt_state) || (bs.charge_state != prev_bs.charge_state)) {
		// Set battery charge condition icon
		switch (bs.batt_state) {
			case BATT_100:
				strcpy(&batt_buf[0], LV_SYMBOL_BATTERY_FULL);
				break;
			case BATT_75:
				strcpy(&batt_buf[0], LV_SYMBOL_BATTERY_3);
				break;
			case BATT_50:
				strcpy(&batt_buf[0], LV_SYMBOL_BATTERY_2);
				break;
			case BATT_25:
				strcpy(&batt_buf[0], LV_SYMBOL_BATTERY_1);
				break;
			default:
				strcpy(&batt_buf[0], LV_SYMBOL_BATTERY_EMPTY);
				break;
		}
	
		// Set charge/fault icon (including null terminator)
		switch (bs.charge_state) {
			case CHARGE_OFF:
			case CHARGE_DONE:
				batt_buf[3] = 0;
				break;
			case CHARGE_ON:
				sprintf(&batt_buf[3], " %s", "#ffffff " LV_SYMBOL_CHARGE "#");
				break;
			default:
				sprintf(&batt_buf[3], " %s", "#ff0000 " LV_SYMBOL_WARNING "#");
				break;
		}
	
		lv_label_set_static_text(lbl_batt_status, batt_buf);
	
		prev_bs.batt_voltage = bs.batt_voltage;
		prev_bs.batt_state = bs.batt_state;
		prev_bs.charge_state = bs.charge_state;
	}
}


static void update_sdcard()
{
	int present;
	static int prev_sdcard_present = -1;   // Force initial update
	static char sdcard_buf[4];   // Statically allocated for "   "0 or sss0
	static char browse_buf[13];  // Statically allocated, big enough for recolor symbol + symbol '#XXXXXX sss#0'
	
	present = file_card_present() ? 1 : 0;
	
	if (present != prev_sdcard_present) {
		if (present) {
			sprintf(sdcard_buf, LV_SYMBOL_SD_CARD);
			sprintf(browse_buf, "#FFFFFF " LV_SYMBOL_DIRECTORY "#");
			lv_obj_set_click(btn_browse, true);
		} else {
			sprintf(sdcard_buf, "   ");
			sprintf(browse_buf, "#808080 " LV_SYMBOL_DIRECTORY "#");
			lv_obj_set_click(btn_browse, false);
		}
		lv_label_set_static_text(lbl_sdcard_status, sdcard_buf);
		lv_label_set_static_text(btn_browse_label, browse_buf);
		prev_sdcard_present = present;
	}
}


static void update_time()
{
	static char time_buf[6];  // Statically allocated for lv_label_set_static_text
	
	tmElements_t tm;
	
	time_get(&tm);
	time_get_hhmm_string(tm, time_buf);
	lv_label_set_static_text(lbl_time, time_buf);
}


static void update_wifi()
{
	bool connected_to_host;
	bool ssid_different;
	bool sta_mode;
	wifi_info_t* wifi_infoP;
	static bool prev_connected_to_host = false;
	// Statically allocated for lv_label_set_static_text = "#CCCCCC <symbol_3># + <ssid>" + null
	static char wifi_label[PS_SSID_MAX_LEN + 14];
	static char prev_ssid[PS_SSID_MAX_LEN];
	static uint8_t prev_flags = 0;
	
	wifi_infoP = wifi_get_info();
	
	sta_mode = ((wifi_infoP->flags & WIFI_INFO_FLAG_CLIENT_MODE) != 0);
	connected_to_host = cmd_connected();
	
	if (sta_mode) {
		ssid_different = (strcmp(prev_ssid, wifi_infoP->sta_ssid) != 0);
	} else {
		ssid_different = (strcmp(prev_ssid, wifi_infoP->ap_ssid) != 0);
	}
	
	if (ssid_different || (prev_flags != wifi_infoP->flags) || (connected_to_host != prev_connected_to_host)) {
	    
		// Update the label with the WiFi Icon and optional SSID to indicate active/connected
		memset(wifi_label, 0, sizeof(wifi_label));
		if ((wifi_infoP->flags & WIFI_INFO_FLAG_ENABLED) == 0) {
			// Dim Icon if interface disabled
			sprintf(wifi_label, "#808080 %s#", LV_SYMBOL_WIFI);
		} else if (sta_mode) {
			// Client Mode:
			//  - Dim White Icon if enabled but not successfully connected to an ap
			//  - Bright White Icon if connected to an AP but not connected to a host
			//  - Bright Green Icon if connected to an AP and connected to a host
			if (connected_to_host) {
				sprintf(wifi_label, "#00FF00 %s %s#", LV_SYMBOL_WIFI, wifi_infoP->sta_ssid);
			} else if ((wifi_infoP->flags & WIFI_INFO_FLAG_CONNECTED) != 0) {
				sprintf(wifi_label, "#FFFFFF %s %s#", LV_SYMBOL_WIFI, wifi_infoP->sta_ssid);
			} else {
				sprintf(wifi_label, "#808080 %s %s#", LV_SYMBOL_WIFI, wifi_infoP->sta_ssid);
			}
		} else {
			// AP Mode: 
			//  - Dim White Icon if interface disabled
			//  - Bright White Icon if enabled but not connected to a host
			//  - Bright Green Icon if enabled and connected to a host
			if (connected_to_host) {
				sprintf(wifi_label, "#00FF00 %s %s#", LV_SYMBOL_WIFI, wifi_infoP->ap_ssid);
			} else if ((wifi_infoP->flags & WIFI_INFO_FLAG_ENABLED) != 0) {
				sprintf(wifi_label, "#FFFFFF %s %s#", LV_SYMBOL_WIFI, wifi_infoP->ap_ssid);
			} else {
				sprintf(wifi_label, "#808080 %s %s#", LV_SYMBOL_WIFI, wifi_infoP->ap_ssid);
			}
		}
		
		lv_label_set_static_text(lbl_ssid, wifi_label);
		
		if (sta_mode) {
			strcpy(prev_ssid, wifi_infoP->sta_ssid);
		} else {
			strcpy(prev_ssid, wifi_infoP->ap_ssid);
		}
		prev_flags = wifi_infoP->flags;
		prev_connected_to_host = connected_to_host;
	}
}


static void update_recording_label()
{
	if (gui_st.record_mode) {
		// Recording mode
		if (gui_st.recording) {
			lv_obj_set_hidden(lbl_recording, false);
		} else {
			lv_obj_set_hidden(lbl_recording, true);
		}
	} else {
		// Picture mode
		lv_obj_set_hidden(lbl_recording, true);
	}
}


static void update_colormap()
{
	lv_point_t points[2];
	int i;
	
	points[0].x = MAIN_CMAP_PALETTE_X1;
	points[1].x = MAIN_CMAP_PALETTE_X2;
	
	// Draw color map top -> bottom / hot -> cold
	for (i=0; i<256; i++) {
		points[0].y = i + (MAIN_CMAP_CANVAS_HEIGHT - 256)/2;
		points[1].y = points[0].y;
		colormap_entry_style.line.color = (lv_color_t) PALETTE_LOOKUP(255-i);
		lv_canvas_draw_line(canvas_colormap, points, 2, &colormap_entry_style);
	}
}


static void update_colormap_temps(lep_buffer_t* sys_bufP, bool init)
{
	static bool prev_agc = true;
	static bool prev_none = true;
	static float prev_tmin = 999;
	static float prev_tmax = -999;
	
	char buf[8];
	float tmin, tmax;
	
	// Reset state when we need to force a redraw later
	if (init) {
		prev_none = true;
		return;
	}
	
	if (gui_st.agc_enabled) {
		// No temperature data when AGC is running
		if (!prev_agc || prev_none) {
			lv_canvas_draw_rect(canvas_colormap, 0, 2, 50, 18, &colormap_clear_temp_style);
			lv_canvas_draw_rect(canvas_colormap, 0, 280, 50, 18, &colormap_clear_temp_style);
			lv_canvas_draw_text(canvas_colormap, 5, 280, 45, &colormap_temp_style, "AGC", LV_LABEL_ALIGN_LEFT);
			prev_agc = true;
			prev_none = false;
			prev_tmin = 999;
			prev_tmax = -999;
		}
	} else if (!gui_st.is_radiometric) {
		// No temperature data for non-radiometric cameras
		if (prev_agc || prev_none) {
			lv_canvas_draw_rect(canvas_colormap, 0, 2, 50, 18, &colormap_clear_temp_style);
			lv_canvas_draw_rect(canvas_colormap, 0, 280, 50, 18, &colormap_clear_temp_style);
			lv_canvas_draw_text(canvas_colormap, 5, 280, 45, &colormap_temp_style, "RAW", LV_LABEL_ALIGN_LEFT);
			prev_agc = false;
			prev_none = false;
		}
	} else {
		if (gui_st.man_range_mode) {
			tmin = gui_range_val_to_disp_temp(gui_st.man_range_min);
			tmax = gui_range_val_to_disp_temp(gui_st.man_range_max);
		} else {
			tmin = gui_lep_to_disp_temp(sys_bufP->lep_min_val, gui_st.rad_high_res);
			tmax = gui_lep_to_disp_temp(sys_bufP->lep_max_val, gui_st.rad_high_res);
		}
		
		// High temp
		if (tmax != prev_tmax) {
			gui_sprintf_temp_string(buf, tmax);
			lv_canvas_draw_rect(canvas_colormap, 0, 2, 50, 18, &colormap_clear_temp_style);
			lv_canvas_draw_text(canvas_colormap, 5, 2, 45, &colormap_temp_style, buf, LV_LABEL_ALIGN_LEFT);
			prev_tmax = tmax;
		}
	
		// Low temp
		if (tmin != prev_tmin) {
			gui_sprintf_temp_string(buf, tmin);
			lv_canvas_draw_rect(canvas_colormap, 0, 280, 50, 18, &colormap_clear_temp_style);
			lv_canvas_draw_text(canvas_colormap, 5, 280, 45, &colormap_temp_style, buf, LV_LABEL_ALIGN_LEFT);
			prev_tmin = tmin;
		}
		
		prev_agc = false;
		prev_none = false;
	}
}


static void update_colormap_marker(lep_buffer_t* sys_bufP)
{
	static bool prev_spotmeter = false;
	static int prev_marker_offset = 0;    // Used to erase the last marker
	int marker_offset;
	uint16_t min_val, max_val;
	
	if (gui_st.agc_enabled || !gui_st.is_radiometric || !gui_st.spotmeter_enable) {
		// No marker when AGC is running or not a radiometric image or the spotmeter is disabled
		if (prev_spotmeter) {
			// Clear marker
			set_spot_marker(prev_marker_offset, true);
			prev_spotmeter = false;
		}
	} else {
		// Select min/max values
		if (gui_st.man_range_mode) {
			// Static user-set range
			if (gui_st.rad_high_res) {
				min_val = gui_st.man_range_min;
				max_val = gui_st.man_range_max;
			} else {
				min_val = gui_st.man_range_min / 10;
				max_val = gui_st.man_range_max / 10;
			}
		} else {
			// Dynamic range from image
			min_val = sys_bufP->lep_min_val;
			max_val = sys_bufP->lep_max_val;
		}
		
		// Update marker
		marker_offset = 255 * (max_val - sys_bufP->lep_telemP[LEP_TEL_SPOT_MEAN]) / (max_val - min_val);
		if (marker_offset < 0) marker_offset = 0;
		if (marker_offset > 255) marker_offset = 255;
		set_spot_marker(prev_marker_offset, true);
		set_spot_marker(marker_offset, false);
		prev_marker_offset = marker_offset;
		prev_spotmeter = true;
	}
}


static void update_spot_temp(lep_buffer_t* sys_bufP, bool init)
{
	static bool displayed = true;
	static char buf[8];                   // Temp for lv_label_set_static_text
	float t;
	
	if (gui_st.spotmeter_enable && gui_st.is_radiometric && !init) {
		// Update temperature display
		t = gui_lep_to_disp_temp(sys_bufP->lep_telemP[LEP_TEL_SPOT_MEAN], gui_st.rad_high_res);
		gui_sprintf_temp_string(buf, t);
		gui_print_static_centered_text(lbl_spot_temp, MAIN_SPOT_TEMP_LBL_X, MAIN_SPOT_TEMP_LBL_Y, buf);
		displayed = true;
	} else {
		if (displayed) {
			// Clear temperature display
			buf[0] = 0;
			lv_label_set_static_text(lbl_spot_temp, buf);
		}
		displayed = false;
	}
}


static void update_agc_btn_label()
{
	static char label_buf[4];  // Statically allocated, big enough for 'AGC0' or 'RAD0'
	
	if (gui_st.agc_enabled) {
		if (gui_st.is_radiometric) {
			strcpy(&label_buf[0], "RAD");
		} else{
			strcpy(&label_buf[0], "RAW");
		}
	} else {
		strcpy(&label_buf[0], "AGC");
	}
	label_buf[3] = 0;
	lv_label_set_static_text(btn_agc_label, label_buf);
}


static void update_range_mode_btn_label()
{
	static char label_buf[12];  // Statically allocated, big enough for recolor symbol '#XXXXXX XX#0'
	
	// Determine brightness of label
	if (gui_st.agc_enabled || !gui_st.is_radiometric) {
		// Dimmer when using a non-radiometric camera, or AGC is enabled
		strcpy(&label_buf[0], "#808080 ");
	} else {
		// Brighter when control is enabled
		strcpy(&label_buf[0], "#FFFFFF ");
	}
	
	// Update control
	if (gui_st.man_range_mode) {
		strcpy(&label_buf[8], "AR#");
	} else {
		strcpy(&label_buf[8], "MR#");
	}
	
	// Terminate and display string
	label_buf[11] = 0;
	lv_label_set_static_text(btn_range_mode_label, label_buf);
}


static void cb_lepton_image(lv_obj_t * img, lv_event_t event)
{
	lv_indev_t* touch;          // Input device
	lv_point_t cur_point;
	uint16_t r1, c1, r2, c2;
	
	if ((event == LV_EVENT_PRESSED) && gui_st.is_radiometric) {
		touch = lv_indev_get_act();
		lv_indev_get_point(touch, &cur_point);

		// Compute a valid ROI
		r1 = (cur_point.y - MAIN_IMG_Y)/2;
		if (r1 >= LEP_HEIGHT-1) r1 = LEP_HEIGHT - 2;
		c1 = (cur_point.x - MAIN_IMG_X)/2;
		if (c1 >= LEP_WIDTH-1) c1 = LEP_WIDTH - 2;
		r2 = r1 + 1;
		c2 = c1 + 1;
		
		lepton_spotmeter(r1, c1, r2, c2);
		
		if (!gui_st.spotmeter_enable) {
			gui_st.spotmeter_enable = true;
			ps_set_gui_state(&gui_st);
		}
	}
}


static void cb_sw_record_mode(lv_obj_t * sw, lv_event_t event)
{
	if (event == LV_EVENT_VALUE_CHANGED) {
		if (gui_st.recording) {
			// Stop recording if switch changed - this should only happen if the switch
			// change is a result of a user action
			xTaskNotify(task_handle_app, APP_NOTIFY_GUI_STOP_RECORD_MASK, eSetBits);
		}
		gui_st.record_mode = lv_sw_get_state(sw);
		update_recording_label();
	}
}


static void cb_btn_browse(lv_obj_t * btn, lv_event_t event)
{
	int last_file_index;
	
	if (event == LV_EVENT_CLICKED) {
		last_file_index = file_get_num_files() - 1;
		if (last_file_index >= 0) {
			if (gui_st.recording) {
				// Stop recording if pressed
				xTaskNotify(task_handle_app, APP_NOTIFY_GUI_STOP_RECORD_MASK, eSetBits);
			}
			
			// Setup to view the last image
			gui_screen_view_set_file_info(last_file_index);
			gui_screen_view_state_init();  // Set VIEW state from our current state
			gui_set_screen(GUI_SCREEN_VIEW);
		}
	}
}


static void cb_btn_setup(lv_obj_t * btn, lv_event_t event)
{
	if (event == LV_EVENT_CLICKED) {
		gui_set_screen(GUI_SCREEN_SETTINGS);
	}
}


static void cb_btn_ffc(lv_obj_t * btn, lv_event_t event)
{
	if (event == LV_EVENT_CLICKED) {
		lepton_ffc();
	}
}


static void cb_btn_agc(lv_obj_t * btn, lv_event_t event)
{
	lep_config_t* lep_stP = lep_get_lep_st();
	
	if (event == LV_EVENT_CLICKED) {
		// Toggle between AGC and Radiometric modes
		lep_stP->agc_set_enabled = !lep_stP->agc_set_enabled;
		lepton_agc(lep_stP->agc_set_enabled);
		
		// Set flag to force update of controls when telemetry state indicates change has occurred
		agc_updated = true;
		
		// Update persistent storage
		ps_set_lep_state(lep_stP);
	}
}


static void cb_btn_poweroff(lv_obj_t * btn, lv_event_t event)
{
	if (event == LV_EVENT_CLICKED) {
		// Let the application task know shutdown was requested
		xTaskNotify(task_handle_app, APP_NOTIFY_SHUTDOWN_MASK, eSetBits);
	}
}


static void cb_btn_range_mode(lv_obj_t * btn, lv_event_t event)
{
	float t;
	
	if (event == LV_EVENT_CLICKED) {
		// Only do something if AGC is not enabled
		if (!gui_st.agc_enabled) {
			// Toggle range mode
			gui_st.man_range_mode = !gui_st.man_range_mode;
			
			// Auto-compute manual range limits if necessary
			if (gui_st.man_range_mode) {
				// Convert min to current display units and round down
				t = gui_lep_to_disp_temp(cur_lep_min_val, gui_st.rad_high_res);
				gui_st.man_range_min = gui_man_range_val_to_lep((uint16_t) t);
				
				// Convert max to current display units and round up
				t = gui_lep_to_disp_temp(cur_lep_max_val, gui_st.rad_high_res);
				gui_st.man_range_max = gui_man_range_val_to_lep((uint16_t) t+1);
			}
			
			// Update label
			update_range_mode_btn_label();
			
			// Update persistent storage
			ps_set_gui_state(&gui_st);
		}
	}
}


static void cb_spot_temp(lv_obj_t * btn, lv_event_t event)
{
	if (event == LV_EVENT_PRESSED) {
		gui_st.spotmeter_enable = false;
		ps_set_gui_state(&gui_st);
	}
}


static void cb_colormap_canvas(lv_obj_t * btn, lv_event_t event)
{
	lv_indev_t* touch;          // Input device
	lv_point_t cur_point;
	
	if (event == LV_EVENT_PRESSED) {
		touch = lv_indev_get_act();
		lv_indev_get_point(touch, &cur_point);
		if (cur_point.y < 160) {
			if (++gui_st.palette >= PALETTE_COUNT) gui_st.palette = 0;
		} else {
			if (gui_st.palette == 0) {
				gui_st.palette = PALETTE_COUNT - 1;
			} else {
				--gui_st.palette;
			}
		}
		set_colormap(gui_st.palette);
		ps_set_gui_state(&gui_st);
	}
}


static void task_info_message_timeout(lv_task_t * task)
{
	// Blank info message
	info_message[0] = 0;
	lv_label_set_static_text(lbl_info_message, info_message);
	
	// Throw away the reference to this task (it is deleted)
	lbl_info_message_task = NULL;
}
