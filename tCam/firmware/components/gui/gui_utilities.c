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
#include <math.h>
#include "gui_utilities.h"
#include "gui_task.h"
#include "lepton_utilities.h"
#include "ps_utilities.h"
#include "sys_utilities.h"
#include "gui_screen_settings.h"
#include "palettes.h"
#include "system_config.h"

//
// GUI Utilities variables
//


// Global GUI state
gui_state_t gui_st;
gui_num_entry_screen_st_t num_entry_st;
lep_into_t lep_info;

// MessageBox
static char preset_msgbox_string[GUI_MSG_BOX_MAX_LEN];
static bool preset_msgbox_dual_btn;

// LVGL objects
static lv_obj_t*  msg_box_bg;
static lv_obj_t*  msg_box;

// Message box buttons
static const char* msg_box_buttons1[] = {"Ok", ""};
static const char* msg_box_buttons2[] = {"Cancel", "Confirm", ""};



//
// GUI Utilities Forward Declarations for internal functions
//
static void gui_message_box(lv_obj_t* parent, const char* msg, bool dual_btn);
static void mbox_event_callback(lv_obj_t *obj, lv_event_t evt);



//
// GUI Utilities API
//
void gui_state_init()
{
	// Get values from persistent storage
	ps_get_gui_state(&gui_st);
	
	// Setup a global theme and Initialize the underlying screen object
	gui_st.gui_theme = lv_theme_night_init(GUI_THEME_HUE, NULL);
	lv_theme_set_current(gui_st.gui_theme);
	
	// Set remaining items to default values
	gui_st.agc_enabled = false;
	gui_st.is_radiometric = false;
	gui_st.rad_high_res = false;
	gui_st.record_mode = false;
	gui_st.recording = false;
	
	// Setup to get lepton info
	lep_info.valid = false;
	
	// Message box starts off not displayed
	msg_box_bg = NULL;
	msg_box = NULL;
}


/**
 * Look for the radiometric configuration values in the telemetry.  Assume we have a
 * radiometric camera if any are non-zero (the Lepton 3.0 returns 0 for all values).
 */
bool gui_tel_is_radiometric(uint16_t* tel)
{
	int i;
	
	// We check the 8 words starting with LEP_TEL_EMISSIVITY for any non-zero values
	for (i=LEP_TEL_EMISSIVITY; i<LEP_TEL_EMISSIVITY+8; i++) {
		if (*(tel+i) != 0) {
			return true;
		}
	}
	
	return false;
}


/**
 * Set the string for gui_preset_message_box - this function is designed to be called
 * by another task who then sends a GUI_NOTIFY_MESSAGEBOX_MASK to gui_task to initiate
 * the message box
 */
void gui_preset_message_box_string(const char* msg, bool dual_btn)
{
	char c;
	int i = 0;
	
	// Copy up to GUI_MSG_BOX_MAX_LEN-1 characters (leaving room for null)
	while (i<GUI_MSG_BOX_MAX_LEN-1) {
		c = *(msg+i);
		preset_msgbox_string[i++] = c;
		if (c == 0) break;
	}
	preset_msgbox_string[i] = 0;
	
	preset_msgbox_dual_btn = dual_btn;
}


/**
 * Display a message box with the preset string - be sure to set the string first!!!
 */
void gui_preset_message_box(lv_obj_t* parent)
{
	gui_message_box(parent, preset_msgbox_string, preset_msgbox_dual_btn);
}


/**
 * Trigger a close of the message box
 */
void gui_close_message_box()
{
	if (msg_box_bg != NULL) {
		lv_mbox_start_auto_close(msg_box, 0);
	}
}


/**
 * Return true if message box is still displayed
 */
bool gui_message_box_displayed()
{
	return (msg_box_bg != NULL);
}



/**
 * Display a centered text string in the label object
 */
void gui_print_static_centered_text(lv_obj_t* obj, int x, int y, char* s)
{
	lv_coord_t w;
	
	lv_label_set_static_text(obj, s);
	w = lv_obj_get_width(obj);
	lv_obj_set_pos(obj, x - (w/2), y);
}


/**
 * Convert a lepton radiometric value to the current units displayable temperature
 */
float gui_lep_to_disp_temp(uint16_t v, bool rad_high_res)
{
	float t;
	
	if (rad_high_res) {
		t = lepton_kelvin_to_C(v, 0.01);
	} else {
		t = lepton_kelvin_to_C(v, 0.1);
	}

	// Convert to F if required
	if (!gui_st.temp_unit_C) {
		t = t * 9.0 / 5.0 + 32.0;
	}
	
	return t;
}


/**
 * Convert a range value to the current units displayable temperature
 */
float gui_range_val_to_disp_temp(int v)
{
	float t;
	
	t = lepton_kelvin_to_C(v, 0.01);

	// Convert to F if required
	if (!gui_st.temp_unit_C) {
		t = t * 9.0 / 5.0 + 32.0;
	}
	
	return t;
}


/**
 * Display a temperature scaled and formatted to fit one of the following
 * formats:
 *    -XXXC  -XXXF
 *   -XX.XC -XX.XF
 *    XX.XC  XX.XF
 *     XXXC   XXXF
 *
 * Input is in C
 */
void gui_sprintf_temp_string(char* buf, float t)
{
	char U = 'C';
	
	// Convert to F if required
	if (!gui_st.temp_unit_C) {
		U = 'F';
	}
  
	if ((t <= -99.95) || (t >= 99.95)) {
		if (t <= -1000) t = -999;
		if (t >= 1000) t = 999;
		sprintf(buf, "%3d%c", (int) round(t), U);
	} else {
		sprintf(buf, "%3.1f%c", t, U);
	}
}


/**
 * Convert a GUI control range value to native K * 100 range value
 *
 */
int gui_man_range_val_to_lep(int gui_val)
{
	float t;
	int lep_val;
	
	// Convert to C if required
	if (gui_st.temp_unit_C) {
		t = gui_val;
	} else {
		t = 5.0 * (gui_val - 32) / 9.0;
	}
	
	lep_val = (int) round((t + 273.15) * 100);
    return lep_val;
}



//
// GUI Utilities internal functions
//

/**
 * Display a message box with at least one button for dismissal
 */
static void gui_message_box(lv_obj_t* parent, const char* msg, bool dual_btn)
{
	static lv_style_t modal_style;   // Message box background style
	
	// Create a full-screen background
	lv_style_copy(&modal_style, &lv_style_plain_color);
	
	// Set the background's style
	modal_style.body.main_color = modal_style.body.grad_color = LV_COLOR_BLACK;
	modal_style.body.opa = LV_OPA_50;
	
	// Create a base object for the modal background 
	msg_box_bg = lv_obj_create(parent, NULL);
	lv_obj_set_style(msg_box_bg, &modal_style);
	lv_obj_set_pos(msg_box_bg, 0, 0);
	lv_obj_set_size(msg_box_bg, LV_HOR_RES, LV_VER_RES);
	lv_obj_set_opa_scale_enable(msg_box_bg, true); // Enable opacity scaling for the animation
	lv_obj_set_event_cb(msg_box_bg, mbox_event_callback);
	
	// Create the message box as a child of the modal background
	msg_box = lv_mbox_create(msg_box_bg, NULL);
	if (dual_btn) {
		lv_mbox_add_btns(msg_box, msg_box_buttons2);
	} else {
		lv_mbox_add_btns(msg_box, msg_box_buttons1);
	}
	lv_mbox_set_text(msg_box, msg);
	lv_obj_set_size(msg_box, GUI_MSG_BOX_W, GUI_MSG_BOX_H);
	lv_obj_align(msg_box, NULL, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_event_cb(msg_box, mbox_event_callback);
	
	// Fade the message box in with an animation
	lv_anim_t a;
	lv_anim_init(&a);
	lv_anim_set_time(&a, 500, 0);
	lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
	//lv_anim_set_exec_cb(&a, msg_box_bg, (lv_anim_exec_xcb_t)lv_obj_set_opa_scale);
	lv_anim_set_exec_cb(&a, msg_box_bg, (void *)lv_obj_set_opa_scale);
	lv_anim_create(&a);
}


/**
 * Message Box callback handling closure and deferred object deletion
 */
static void mbox_event_callback(lv_obj_t *obj, lv_event_t event)
{
	if (event == LV_EVENT_DELETE) {
		if (obj == msg_box_bg) {
			msg_box_bg = NULL;
		} else if (obj == msg_box) {
			// Delete the parent modal background
			lv_obj_del_async(lv_obj_get_parent(msg_box));
			msg_box = NULL; // happens before object is actually deleted!
		}
	} else if (event == LV_EVENT_VALUE_CHANGED) {
		// Let gui_task know a button was clicked
		gui_set_msgbox_btn(lv_mbox_get_active_btn(obj));
		
		// Dismiss the message box
		lv_mbox_start_auto_close(msg_box, 0);
	}
}
