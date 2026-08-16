/*
 * GUI Task
 *
 * Contains functions to initialize the LittleVGL GUI system and a task
 * to evaluate its display related sub-tasks.  The GUI Task is responsible
 * for all access (updating) of the GUI managed by LittleVGL.
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
 * along with firecam.  If not, see <https://www.gnu.org/licenses/>.
 *
 */
#include "gui_task.h"
#include "app_task.h"
#include "lep_task.h"
#include "rsp_task.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_freertos_hooks.h"
#include "system_config.h"
#include "disp_spi.h"
#include "disp_driver.h"
#include "touch_driver.h"
#include "sys_utilities.h"
#include "gui_screen_main.h"
#include "gui_screen_settings.h"
#include "gui_screen_num_entry.h"
#include "gui_screen_emissivity.h"
#include "gui_screen_info.h"
#include "gui_screen_network.h"
#include "gui_screen_time.h"
#include "gui_screen_wifi.h"
#include "gui_screen_view.h"
#include "gui_screen_browse.h"
#include "gui_screen_update.h"
#include "gui_utilities.h"
#ifdef SYS_SCREENDUMP_ENABLE
#include "rsp_task.h"
#include "mem_fb.h"
#endif



//
// GUI Task internal constants
//

// LVGL sub-task indicies
#define LVGL_ST_EVENT       0
#define LVGL_ST_MESSAGEBOX  1
#define LVGL_ST_NUM         2


//
// GUI Task variables
//
static const char* TAG = "gui_task";

// Dual display update buffers to allow DMA/SPI transfer of one while the other is updated
static lv_color_t lvgl_disp_buf1[LVGL_DISP_BUF_SIZE];
static lv_color_t lvgl_disp_buf2[LVGL_DISP_BUF_SIZE];
static lv_disp_buf_t lvgl_disp_buf;

// Display driver
static lv_disp_drv_t lvgl_disp_drv;

// Touchscreen driver
static lv_indev_drv_t lvgl_indev_drv;

// Screen object array and current screen index
static lv_obj_t* gui_screens[GUI_NUM_SCREENS];
static int gui_cur_screen_index = -1;

// LVGL sub-task array
static lv_task_t* lvgl_tasks[LVGL_ST_NUM];

// FPS averaging array (mSec between )
static int64_t prev_fps_sample_time;
static int fps_array_index;
static int fps_delta_msec_array[GUI_NUM_FPS_SAMPLES];

// Filename to display for recorded images
static char record_filename[20];

// Request to display message box
static bool req_message_box;
static bool fw_req_upd_message_box = false;

// Playback timestamp destined for gui_screen_view
static uint32_t video_ts;


//
// GUI Task internal function forward declarations
//
static bool gui_lvgl_init();
static void gui_screen_init();
static void gui_add_subtasks();
static void gui_task_event_handler_task(lv_task_t * task);
static void gui_task_messagebox_handler_task(lv_task_t * task);
static void IRAM_ATTR lv_tick_callback();
static void gui_init_fps();
static void gui_update_fps();
#ifdef SYS_SCREENDUMP_ENABLE
static void gui_do_screendump();
#endif



//
// GUI Task API
//

/**
 * GUI Task - Executes all GUI/display related activities via LittleVGL objects
 * and LittleVGL sub-tasks evaluated by lv_task_handler.  Communication with other
 * tasks is handled in this routine although various LVGL callbacks and event code
 * may call routines in other modules to get or set state.
 */
void gui_task()
{
	ESP_LOGI(TAG, "Start task");

	// Initialize
	if (!gui_lvgl_init()) {
		vTaskDelete(NULL);
	}
	gui_state_init();
	gui_screen_init();
	gui_add_subtasks();
	gui_init_fps();
	
	// Module variables
	req_message_box = false;
	
	// Set the initially displayed screen
	gui_set_screen(GUI_SCREEN_MAIN);
	
	while (1) {
		// This task runs every GUI_EVAL_MSEC mSec
		vTaskDelay(pdMS_TO_TICKS(GUI_EVAL_MSEC));
		lv_task_handler();
	}
}


/**
 * Set the currently displayed screen
 */
void gui_set_screen(int n)
{
	if (n < GUI_NUM_SCREENS) {
		if ((gui_cur_screen_index != GUI_SCREEN_MAIN) && (n == GUI_SCREEN_MAIN)) {
			if (task_handle_lep != NULL) {
				xTaskNotify(task_handle_lep, LEP_NOTIFY_EN_GUI_FRAME_MASK, eSetBits);
			}
		}
		if ((gui_cur_screen_index == GUI_SCREEN_MAIN) && (n != GUI_SCREEN_MAIN)) {
			if (task_handle_lep != NULL) {
				xTaskNotify(task_handle_lep, LEP_NOTIFY_DIS_GUI_FRAME_MASK, eSetBits);
			}
		}
	
		gui_cur_screen_index = n;
		
		gui_screen_main_set_active(n == GUI_SCREEN_MAIN);
		gui_screen_settings_set_active(n == GUI_SCREEN_SETTINGS);
		gui_screen_num_entry_set_active(n == GUI_SCREEN_NUM_ENTRY);
		gui_screen_emissivity_set_active(n == GUI_SCREEN_EMISSIVITY);
		gui_screen_info_set_active(n == GUI_SCREEN_INFO);
		gui_screen_network_set_active(n == GUI_SCREEN_NETWORK);
		gui_screen_time_set_active(n == GUI_SCREEN_TIME);
		gui_screen_wifi_set_active(n == GUI_SCREEN_WIFI);
		gui_screen_view_set_active(n == GUI_SCREEN_VIEW);
		gui_screen_browse_set_active(n == GUI_SCREEN_BROWSE);
		gui_screen_update_set_active(n == GUI_SCREEN_UPDATE);
		
		lv_scr_load(gui_screens[n]);
	}
}


/**
 * Set the filename of the last image recorded for us to display when notified
 */
void gui_set_write_filename(char* name)
{
	char c;
	int i = 0;
	int n;
	
	n = sizeof(record_filename);
	c = *name;
	while ((i < (n-1)) && (c != 0)) {
		record_filename[i++] = c;
		c = *(name + i);
	}
	record_filename[i] = 0; // terminate
}


/**
 * Set the btn pressed in a messagebox - used to trigger activity in response
 * to specific buttons
 */
void gui_set_msgbox_btn(uint16_t btn)
{
	if (fw_req_upd_message_box) {
		// Special case for messagebox requesting user if they want to perform a FW
		// update which can occur in any screen
		if (btn == GUI_MSG_BOX_BTN_AFFIRM) {
			// User wants to update FW - let rsp_task know so it can try to start the update
			xTaskNotify(task_handle_rsp, RSP_NOTIFY_FW_UPD_EN_MASK, eSetBits);
		}
		fw_req_upd_message_box = false;
	} else {
		// Call screens that want to know which button
		switch (gui_cur_screen_index) {
			case GUI_SCREEN_BROWSE:
				gui_screen_browse_set_msgbox_btn(btn);
				break;
			
			case GUI_SCREEN_VIEW:
				gui_screen_view_set_msgbox_btn(btn);
				break;
			
			case GUI_SCREEN_UPDATE:
				gui_screen_update_set_msgbox_btn(btn);
				break;
				
			case GUI_SCREEN_INFO:
				gui_screen_info_set_msgbox_btn(btn);
				break;
		}
	}
}


/**
 * Set a timestamp value to pass on to gui_screen_view as part of a video playback
 */
void gui_set_playback_ts(uint32_t ts)
{
	video_ts = ts;
}



//
// GUI Task Internal functions
//

/**
 * Initialize the LittleVGL system including initializing the LCD display and
 * Touchscreen controller.
 */
static bool gui_lvgl_init()
{
	// Initialize lvgl
	lv_init();
	
	//
	// Interface and driver initialization
	//
	disp_driver_init(true);
	touch_driver_init();
	
	// Install the display driver
	lv_disp_buf_init(&lvgl_disp_buf, lvgl_disp_buf1, lvgl_disp_buf2, LVGL_DISP_BUF_SIZE);
	lv_disp_drv_init(&lvgl_disp_drv);
	lvgl_disp_drv.flush_cb = disp_driver_flush;
	lvgl_disp_drv.buffer = &lvgl_disp_buf;
	lv_disp_drv_register(&lvgl_disp_drv);
	
	// Install the touchscreen driver
    lv_indev_drv_init(&lvgl_indev_drv);
    lvgl_indev_drv.read_cb = touch_driver_read;
    lvgl_indev_drv.type = LV_INDEV_TYPE_POINTER;
    lv_indev_drv_register(&lvgl_indev_drv);
    
    // Hook LittleVGL's timebase to its CPU system tick so it can keep track of time
    esp_register_freertos_tick_hook(lv_tick_callback);
    
    return true;
}



/**
 * Initialize the screen objects and their control callbacks
 */
static void gui_screen_init()
{
	// Initialize the screens
	gui_screens[GUI_SCREEN_MAIN] = gui_screen_main_create();
	gui_screens[GUI_SCREEN_SETTINGS] = gui_screen_settings_create();
	gui_screens[GUI_SCREEN_NUM_ENTRY] = gui_screen_num_entry_create();
	gui_screens[GUI_SCREEN_EMISSIVITY] = gui_screen_emissivity_create();
	gui_screens[GUI_SCREEN_INFO] = gui_screen_info_create();
	gui_screens[GUI_SCREEN_NETWORK] = gui_screen_network_create();
	gui_screens[GUI_SCREEN_TIME] = gui_screen_time_create();
	gui_screens[GUI_SCREEN_WIFI] = gui_screen_wifi_create();
	gui_screens[GUI_SCREEN_VIEW] = gui_screen_view_create();
	gui_screens[GUI_SCREEN_BROWSE] = gui_screen_browse_create();
	gui_screens[GUI_SCREEN_UPDATE] = gui_screen_update_create();
}


/**
 * Add the LittleVGL sub-tasks and specify their evaluation period
 */
static void gui_add_subtasks()
{
	// Event handler sub-task runs every GUI_EVAL_MSEC mSec
	lvgl_tasks[LVGL_ST_EVENT] = lv_task_create(gui_task_event_handler_task, GUI_EVAL_MSEC,
		LV_TASK_PRIO_MID, NULL);
		
	// Message box display sub-task runs every GUI_EVAL_MSEC mSec
	lvgl_tasks[LVGL_ST_MESSAGEBOX] = lv_task_create(gui_task_messagebox_handler_task, GUI_EVAL_MSEC,
		LV_TASK_PRIO_LOW, NULL);
}


/**
 * LittleVGL sub-task to handle events from the other tasks
 */
static void gui_task_event_handler_task(lv_task_t * task)
{
	uint32_t notification_value;
	
	// Look for incoming notifications (clear them upon reading)
	if (xTaskNotifyWait(0x00, 0xFFFFFFFF, &notification_value, 0)) {
		//
		// LEPTON
		//
		if (Notification(notification_value, GUI_NOTIFY_LEP_FRAME_MASK_1)) {
			// lep_task has updated the shared buffer with a new image
			if (gui_cur_screen_index == GUI_SCREEN_MAIN) {
				// Trigger the main screen to draw the image from the buffer to the display
				gui_screen_main_update_lep_image(0);
				
				// Update FPS stats
				gui_update_fps();
				//printf("fps: %1.1f\n", gui_st.fps);
			}
		}
		if (Notification(notification_value, GUI_NOTIFY_LEP_FRAME_MASK_2)) {
			// lep_task has updated the shared buffer with a new image
			if (gui_cur_screen_index == GUI_SCREEN_MAIN) {
				// Trigger the main screen to draw the image from the buffer to the display
				gui_screen_main_update_lep_image(1);
				
				// Update FPS stats
				gui_update_fps();
				//printf("fps: %1.1f\n", gui_st.fps);
			}
		}
		
		//
		// APP
		//
		if (Notification(notification_value, GUI_NOTIFY_RECORD_IMG_MASK)) {
			gui_screen_main_set_image_state(IMG_STATE_PICTURE);
			
			// Display the stored image filename for a second
			gui_screen_display_message(record_filename, 1);
		}
		
		if (Notification(notification_value, GUI_NOTIFY_RECORD_ON_MASK)) {
			gui_screen_main_set_image_state(IMG_STATE_REC_ON);
			
			// Display the stored image filename for a second
			gui_screen_display_message(record_filename, 1);
		}
		
		if (Notification(notification_value, GUI_NOTIFY_RECORD_OFF_MASK)) {
			gui_screen_main_set_image_state(IMG_STATE_REC_OFF);
		}
		
		if (Notification(notification_value, GUI_NOTIFY_RECORD_FAIL_MASK)) {
			gui_screen_main_set_image_state(IMG_STATE_REC_OFF);
		}
		
		if (Notification(notification_value, GUI_NOTIFY_SDCARD_MISSING_MASK)) {
			// Change back to main screen if we're on a screen that depends on the filesystem
			if ((gui_cur_screen_index == GUI_SCREEN_BROWSE) || (gui_cur_screen_index == GUI_SCREEN_VIEW)) {
				gui_set_screen(GUI_SCREEN_MAIN);
			}
		}
		
		if (Notification(notification_value, GUI_NOTIFY_FILE_DEL_FILE_DONE_MASK)) {
			switch (gui_cur_screen_index) {
				case GUI_SCREEN_BROWSE:
					gui_screen_browse_update_after_delete();
					break;
		
				case GUI_SCREEN_VIEW:
					gui_screen_view_update_after_delete();
					break;
			}
		}
		
		if (Notification(notification_value, GUI_NOTIFY_FW_UPD_REQ_MB_MASK)) {
			req_message_box = true;
			fw_req_upd_message_box = true;
		}
		
		if (Notification(notification_value, GUI_NOTIFY_FW_UPD_END_MB_MASK)) {
			gui_close_message_box();
			fw_req_upd_message_box = false;
		}
		
		if (Notification(notification_value, GUI_NOTIFY_FW_UPD_START_MASK)) {
			gui_set_screen(GUI_SCREEN_UPDATE);
		}
		
		if (Notification(notification_value, GUI_NOTIFY_FW_UPD_STOP_MASK)) {
			gui_set_screen(GUI_SCREEN_UPDATE);
		}
		
		//
		// CMD
		//
		if (Notification(notification_value, GUI_NOTIFY_NEW_AGC_MASK)) {
			if (gui_cur_screen_index == GUI_SCREEN_MAIN) {
				gui_screen_agc_updated();
			}
		}
		
		//
		// FILE
		//
		if (Notification(notification_value, GUI_NOTIFY_FILE_CATALOG_READY_MASK)) {
			if (gui_cur_screen_index == GUI_SCREEN_BROWSE) {
				gui_screen_browse_update_list();
			}
		}
		
		if (Notification(notification_value, GUI_NOTIFY_FILE_IMAGE_READY_MASK)) {
			if (gui_cur_screen_index == GUI_SCREEN_VIEW) {
				gui_screen_view_update_image();
			}
		}
		
		if (Notification(notification_value, GUI_NOTIFY_FILE_UPDATE_PB_LEN_MASK)) {
			if (gui_cur_screen_index == GUI_SCREEN_VIEW) {
				gui_screen_view_set_playback_state(VIEW_PB_UPD_LEN, video_ts);
			}
		}
		
		if (Notification(notification_value, GUI_NOTIFY_FILE_UPDATE_PB_TS_MASK)) {
			if (gui_cur_screen_index == GUI_SCREEN_VIEW) {
				gui_screen_view_set_playback_state(VIEW_PB_UPD_POS, video_ts);
			}
		}
		
		if (Notification(notification_value, GUI_NOTIFY_FILE_PB_DONE_MASK)) {
			if (gui_cur_screen_index == GUI_SCREEN_VIEW) {
					gui_screen_view_set_playback_state(VIEW_PB_UPD_STATE_DONE, 0);
			}
		}
		
		//
		// GCORE
		//
		if (Notification(notification_value, GUI_NOTIFY_SNAP_BTN_PRESSED_MASK)) {
			// Can only take a picture or start/stop a recording if the main screen is displayed
			if (gui_cur_screen_index == GUI_SCREEN_MAIN) {
				if (gui_st.record_mode) {
					if (gui_st.recording) {
						xTaskNotify(task_handle_app, APP_NOTIFY_GUI_STOP_RECORD_MASK, eSetBits);
					} else {
						xTaskNotify(task_handle_app, APP_NOTIFY_GUI_START_RECORD_MASK, eSetBits);
					}
				} else {
					xTaskNotify(task_handle_app, APP_NOTIFY_GUI_TAKE_PICTURE_MASK, eSetBits);
				}
			}
		}
		
		//
		// MessageBox Handling
		//
		if (Notification(notification_value, GUI_NOTIFY_MESSAGEBOX_MASK)) {
			req_message_box = true;
		}
		
#ifdef SYS_SCREENDUMP_ENABLE	
		//
		// Screendump
		//
		if (Notification(notification_value, GUI_NOTIFY_SCREENDUMP_MASK)) {
			gui_do_screendump();
		}
#endif
	}
}


/**
 * LittleVGL sub-task to handle events to handle requests to display the messagebox.
 * Since the messagebox has an animation to close we have to make sure it is fully
 * closed from a previous message before triggering it again.
 */
 static void gui_task_messagebox_handler_task(lv_task_t * task)
 {
 	if (req_message_box && !gui_message_box_displayed()) {
 		req_message_box = false;
 		gui_preset_message_box(gui_screens[gui_cur_screen_index]);
 	}
 }


/**
 * LittleVGL timekeeping callback - hooked to the system tick timer so LVGL
 * knows how much time has gone by (used for animations, etc).
 */
static void IRAM_ATTR lv_tick_callback()
{
	lv_tick_inc(portTICK_RATE_MS);
}


/**
 * Frame/sec averaging
 */
static void gui_init_fps()
{
	prev_fps_sample_time = esp_timer_get_time();
	
	for (fps_array_index=0; fps_array_index<GUI_NUM_FPS_SAMPLES; fps_array_index++) {
		fps_delta_msec_array[fps_array_index] = 1000;
	}
	fps_array_index = 0;
}


static void gui_update_fps()
{
	int64_t cur_time = esp_timer_get_time();
	float sum = 0;
	
	// Get the delta time for this displayed image
	fps_delta_msec_array[fps_array_index++] = (cur_time - prev_fps_sample_time) / 1000;
	if (fps_array_index >= GUI_NUM_FPS_SAMPLES) fps_array_index = 0;
	prev_fps_sample_time = cur_time;
	
	// Generate an average
	for (int i=0; i<GUI_NUM_FPS_SAMPLES; i++) {
		sum += fps_delta_msec_array[i];
	}
	sum = sum / GUI_NUM_FPS_SAMPLES;
	
	// Get the average FPS
	gui_st.fps = 1000 / sum;
}


#ifdef SYS_SCREENDUMP_ENABLE
// This task blocks gui_task
void gui_do_screendump()
{
	// Configure the display driver to render to the screendump frame buffer
	disp_driver_en_dump(true);
	
	// Force LVGL to redraw the entire screen (to the screendump frame buffer)
	lv_obj_invalidate(lv_scr_act());
	lv_refr_now(lv_disp_get_default());
	
	// Reconfigure the driver back to the LCD
	disp_driver_en_dump(false);
	
	// Notify rsp_task that the screen dump is available
	xTaskNotify(task_handle_rsp, RSP_NOTIFY_SCREEN_DUMP_AVAIL_MASK, eSetBits);
}
#endif
