/*
 * FW Update GUI screen related functions, callbacks and event handlers
 *
 * Copyright 2021 Dan Julio
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
#include "gui_screen_update.h"
#include "app_task.h"
#include "gui_task.h"
#include "rsp_task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl/lvgl.h"
#include "lv_conf.h"
#include "gui_utilities.h"
#include "sys_utilities.h"
#include "upd_utilities.h"
#include <string.h>



//
// Update FW GUI Screen variables
//
static const char* TAG = "gui_screen_update";

// LVGL objects
static lv_obj_t* update_screen;
static lv_obj_t* lbl_update_title;
static lv_obj_t* btn_back;
static lv_obj_t* btn_back_label;
static lv_obj_t* lbl_version;
static lv_obj_t* bar_progress;
static lv_obj_t* lbl_update_percent;

// Progress bar update timer task
static lv_task_t* update_timer;



//
// Update FW GUI Screen internal function forward declarations
//
static void initialize_screen_values();
static void cb_btn_exit(lv_obj_t * btn, lv_event_t event);
static void cb_update_task(lv_task_t * task);



//
// Update FW GUI Screen API
//
lv_obj_t* gui_screen_update_create()
{
	update_screen = lv_obj_create(NULL, NULL);
	lv_obj_set_size(update_screen, LV_HOR_RES_MAX, LV_VER_RES_MAX);
	
	// Create the graphical elements for this screen
	//
	// Screen Title using a larger font (centered)
	lbl_update_title = lv_label_create(update_screen, NULL);
	static lv_style_t lbl_update_title_style;
	lv_style_copy(&lbl_update_title_style, gui_st.gui_theme->style.bg);
	lbl_update_title_style.text.font = &lv_font_roboto_22;
	lv_label_set_style(lbl_update_title, LV_LABEL_STYLE_MAIN, &lbl_update_title_style);
	gui_print_static_centered_text(lbl_update_title, UPDATE_HEADER_LBL_X, UPDATE_HEADER_LBL_Y, "Firmware Update");
	
	// Exit button
	btn_back = lv_btn_create(update_screen, NULL);
	lv_obj_set_pos(btn_back, UPDATE_EXIT_BTN_X, UPDATE_EXIT_BTN_Y);
	lv_obj_set_size(btn_back, UPDATE_EXIT_BTN_W, UPDATE_EXIT_BTN_H);
	lv_obj_set_event_cb(btn_back, cb_btn_exit);
	btn_back_label = lv_label_create(btn_back, NULL);
	lv_label_set_static_text(btn_back_label, LV_SYMBOL_CLOSE);
	
	// Version label
	lbl_version = lv_label_create(update_screen, NULL);
	lv_obj_set_pos(lbl_version, UPDATE_VER_LBL_X, UPDATE_VER_LBL_Y);
	
	// Progress bar
	bar_progress = lv_bar_create(update_screen, NULL);
	lv_obj_set_pos(bar_progress, UPDATE_PB_X, UPDATE_PB_Y);
	lv_obj_set_size(bar_progress, UPDATE_PB_W, UPDATE_PB_H);
	lv_bar_set_range(bar_progress, 0, 100);
	
	// Update percent
	lbl_update_percent = lv_label_create(update_screen, NULL);
	lv_obj_set_pos(lbl_update_percent, UPDATE_PERCENT_X, UPDATE_PERCENT_Y);

	// Force immediate update of some regions
	initialize_screen_values();
	
	return update_screen;
}


void gui_screen_update_set_active(bool en)
{
	static char buf[15+UPD_MAX_VER_LEN];    // "New Firmware: <VERSION>"
	
	if (en) {
		// Update the version
		sprintf(buf, "New Firmware: %s", rsp_get_fw_upd_version());
		lv_label_set_static_text(lbl_version, buf);
		
		// Reset the progress bar and update percent
		lv_bar_set_value(bar_progress, 0, LV_ANIM_OFF);
		lv_label_set_static_text(lbl_update_percent, "0%");
		
		// Start update timer
		update_timer = lv_task_create(cb_update_task, UPDATE_PROGRESS_MSEC, LV_TASK_PRIO_LOW, NULL);
	} else {
		if (update_timer != NULL) {
			lv_task_del(update_timer);
			update_timer = NULL;
		}
	}
}


void gui_screen_update_set_msgbox_btn(uint16_t btn)
{
	// We're called from the message box displayed when the firmware update either
	// succeeds (two buttons: confirm->reboot, cancel->main screen) or fails
	// (one button: cancel->main screen)
	if (btn == GUI_MSG_BOX_BTN_AFFIRM) {
		// Stop recording (if it is running) and delay momentarily to allow that to occur
		xTaskNotify(task_handle_app, APP_NOTIFY_GUI_STOP_RECORD_MASK, eSetBits);
		vTaskDelay(pdMS_TO_TICKS(250));
		
		// Reboot
		esp_restart();
	} else {
		gui_set_screen(GUI_SCREEN_MAIN);
	}
}



//
// Update FW GUI Screen internal functions
//
static void initialize_screen_values()
{
	lv_label_set_static_text(lbl_version, "New Firmware: ---");
	lv_bar_set_value(bar_progress, 0, LV_ANIM_OFF);
	lv_label_set_static_text(lbl_update_percent, "0%");
	update_timer = NULL;
}


static void cb_btn_exit(lv_obj_t * btn, lv_event_t event)
{
	if (event == LV_EVENT_CLICKED) {
		// Notify rsp_task to stop update
		xTaskNotify(task_handle_rsp, RSP_NOTIFY_FW_UPD_END_MASK, eSetBits);
	}
}


static void cb_update_task(lv_task_t * task)
{
	static char update_percent[5];    // Sized for "100%"
	int16_t percent;
	int16_t cur_percent;
	
	percent = (int16_t) rsp_get_update_percent();
	if (percent < 0) percent = 0;
	if (percent > 100) percent = 100;
	
	cur_percent = lv_bar_get_value(bar_progress);
	
	if (percent != cur_percent) {
		ESP_LOGI(TAG, "Updating to %d %%", percent);
		lv_bar_set_value(bar_progress, percent, LV_ANIM_OFF);
		lv_obj_invalidate(bar_progress);
		sprintf(update_percent, "%d%%", percent);
		lv_label_set_static_text(lbl_update_percent, update_percent);
	}
}