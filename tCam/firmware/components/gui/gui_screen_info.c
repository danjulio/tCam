/*
 * Camera Information GUI screen related functions, callbacks and event handlers
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
#include "app_task.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gcore.h"
#include "gcore_task.h"
#include "gui_screen_info.h"
#include "gui_task.h"
#include "gui_utilities.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "file_utilities.h"
#include "gui_utilities.h"
#include "lepton_utilities.h"
#include "power_utilities.h"
#include "ps_utilities.h"
#include "sys_utilities.h"
#include "wifi_utilities.h"
#include "lv_conf.h"


//
// Camera Information GUI Screen constants
//

#define INFO_MAX_CHARS 1024



//
// Camera Information GUI Screen variables
//
static const char* TAG = "gui_screen_info";

// LVGL objects
static lv_obj_t* info_screen;
static lv_obj_t* lbl_info_title;
static lv_obj_t* btn_info_exit;
static lv_obj_t* btn_info_exit_label;
static lv_obj_t* lbl_cam_info;

//Factory reset button timer task
static lv_task_t* timer_task;

// Wifi state updated on entry for several fields
static wifi_info_t* wifi_info;
static bool ap_mode;

// Dynamically allocated buffer for camera information
static char* cam_info_buf;

// Screen state
static bool press_det_running;
static int press_det_count;



//
// Camera Information GUI Screen internal function forward declarations
//
static void initialize_screen_values();
static void update_info();
static int add_fw_version(int n);
static int add_sdk_version(int n);
//static int add_esp32_chip_version(int n);
static int add_tcam_mini_version(int n);
static int add_gcore_pmic_version(int n);
static int add_lep_info(int n);
static int add_battery_info(int n);
static int add_ip_address(int n);
static int add_mac_address(int n);
static int add_storage_info(int n);
static int add_mem_info(int n);
static int add_fps(int n);
static int add_copyright_info(int n);
static void cb_btn_exit(lv_obj_t * btn, lv_event_t event);
static void cb_reset_btn(lv_obj_t* btn, lv_event_t event);
static void cb_timer(lv_task_t * task);



/**
 * Create the Camera Information screen, its graphical objects and link necessary callbacks
 */
lv_obj_t* gui_screen_info_create()
{
	info_screen = lv_obj_create(NULL, NULL);
	lv_obj_set_size(info_screen, LV_HOR_RES_MAX, LV_VER_RES_MAX);
	
	// Create the graphical elements for this screen
	//
	// Screen Title using a larger font (centered) - enabled to be catch clicks
	lbl_info_title = lv_label_create(info_screen, NULL);
	lv_obj_set_click(lbl_info_title, true);
	lv_obj_set_event_cb(lbl_info_title, cb_reset_btn);
	static lv_style_t lbl_info_title_style;
	lv_style_copy(&lbl_info_title_style, gui_st.gui_theme->style.bg);
	lbl_info_title_style.text.font = &lv_font_roboto_22;
	lv_label_set_style(lbl_info_title, LV_LABEL_STYLE_MAIN, &lbl_info_title_style);
	gui_print_static_centered_text(lbl_info_title, I_TITLE_LBL_X, I_TITLE_LBL_Y, "Camera Information");
	
	// Exit button
	btn_info_exit = lv_btn_create(info_screen, NULL);
	lv_obj_set_pos(btn_info_exit, I_EXIT_BTN_X, I_EXIT_BTN_Y);
	lv_obj_set_size(btn_info_exit, I_EXIT_BTN_W, I_EXIT_BTN_H);
	lv_obj_set_event_cb(btn_info_exit, cb_btn_exit);
	btn_info_exit_label = lv_label_create(btn_info_exit, NULL);
	lv_label_set_static_text(btn_info_exit_label, LV_SYMBOL_CLOSE);
	
	// Camera Info text area
	lbl_cam_info = lv_label_create(info_screen, NULL);
	lv_obj_set_pos(lbl_cam_info, I_INFO_LBL_X, I_INFO_LBL_Y);
	
	initialize_screen_values();
		
	return info_screen;
}


/**
 * Tell this screen if it is newly active or not
 * (activating a screen (re)initializes its display)
 */
void gui_screen_info_set_active(bool en)
{
	if (en) {
		press_det_running = false;
		press_det_count = 0;
		ap_mode = ((wifi_info->flags & WIFI_INFO_FLAG_CLIENT_MODE) == 0);
		update_info();
	}
}


void gui_screen_info_set_msgbox_btn(uint16_t btn)
{
	// We're called from the message box displayed when the user has requested a
	// reset to factory default.  The message box gives them a chance to decide
	// to actually perform the reset or skip it, returning back to the info display
	// (two buttons: confirm->reset and reboot, cancel->remain on screen).
	if (btn == GUI_MSG_BOX_BTN_AFFIRM) {
		// Stop recording (if it is running)
		xTaskNotify(task_handle_app, APP_NOTIFY_GUI_STOP_RECORD_MASK, eSetBits);
		
		// Stop gcore_task from accessing the PMIC/RTC in case we have to write
		// RAM back to flash.  We do this to prevent I2C access from failing since
		// writing flash takes a long time and prevents the PMIC/RTC from responding
		// to I2C while it is occurring.  Probably it doesn't matter if any I2C
		// accesses failed since we are rebooting but this prevents error messages
		// from being logged and potentially freaking someone (me...) out.
		xTaskNotify(task_handle_gcore, GCORE_NOTIFY_HALT_ACCESS_MASK, eSetBits);
		
		// Delay to allow recording to stop if necessary and for I2C access to stop
		vTaskDelay(pdMS_TO_TICKS(200));
		
		// Set persistent storage back to factory default values
		ps_set_factory_default();
		
		// Final display and delay to for final logging
		ESP_LOGI(TAG, "Reboot system");
		vTaskDelay(pdMS_TO_TICKS(50));
		
		// Reboot
		esp_restart();
	}
}



//
// Camera Information GUI Screen internal functions
//

static void initialize_screen_values()
{
	// Allocate memory for our buffer - this should never fail...
	cam_info_buf = heap_caps_malloc(INFO_MAX_CHARS, MALLOC_CAP_SPIRAM);
	if (cam_info_buf == NULL) {
		ESP_LOGE(TAG, "malloc info buffer failed - bailing");
		vTaskDelete(NULL);
	}
	
	// Get a pointer to the wifi state
	wifi_info = wifi_get_info();
	
	// Timer starts off disabled
	press_det_running = false;
	press_det_count = 0;
	timer_task = NULL;
	
	// Initialize displayed on-screen values
	update_info();
}


static void update_info()
{
	int n = 0;
	
	n = add_fw_version(n);
	n = add_sdk_version(n);
//	n = add_esp32_chip_version(n);
	n = add_tcam_mini_version(n);
	n = add_gcore_pmic_version(n);
	n = add_lep_info(n);
	n = add_battery_info(n);
	n = add_ip_address(n);
	n = add_mac_address(n);
	n = add_storage_info(n);
	n = add_mem_info(n);
	n = add_fps(n);
	n = add_copyright_info(n);
	
	lv_label_set_static_text(lbl_cam_info, cam_info_buf);
}


static int add_fw_version(int n)
{
	const esp_app_desc_t* app_desc;
	
	app_desc = esp_ota_get_app_description();
	sprintf(&cam_info_buf[n], "FW Version: %s\n", app_desc->version);
	
	return (strlen(cam_info_buf));
}


static int add_sdk_version(int n)
{
	sprintf(&cam_info_buf[n], "SDK Version: %s\n", esp_get_idf_version());
	
	return (strlen(cam_info_buf));
}


/*
static int add_esp32_chip_version(int n)
{
	esp_chip_info_t esp_desc;
	
	esp_chip_info(&esp_desc);
	sprintf(&cam_info_buf[n], "ESP32 Revision: %d\n", esp_desc.revision);
	
	return (strlen(cam_info_buf));
}
*/


static int add_tcam_mini_version(int n)
{
	sprintf(&cam_info_buf[n], "tCam-Mini Version: %s\n", tcam_mini_status.version);
	
	return (strlen(cam_info_buf));
}


static int add_gcore_pmic_version(int n)
{
	uint8_t reg;
	
	if (gcore_get_reg8(GCORE_REG_VER, &reg)) {
		sprintf(&cam_info_buf[n], "RTC/PMIC Version: %d.%d\n", reg >> 4, reg & 0xF);
	} else {
		sprintf(&cam_info_buf[n], "RTC/PMIC Version: ?\n");
	}
	
	return (strlen(cam_info_buf));
}


static int add_lep_info(int n)
{
	float fpa_t, hse_t;
	
	switch (lepton_get_model()) {
		case LEP_TYPE_3_0:
			sprintf(&cam_info_buf[n], "Lepton 3.0");
			break;
		case LEP_TYPE_3_1:
			sprintf(&cam_info_buf[n], "Lepton 3.1");
			break;
		case LEP_TYPE_3_5:
			sprintf(&cam_info_buf[n], "Lepton 3.5");
			break;
		default:
			sprintf(&cam_info_buf[n], "Lepton Unknown");
	}
	n = strlen(cam_info_buf);
	
	if (lep_info.valid) {
		fpa_t = (float) lep_info.lep_fpa_temp_k100 / 100.0 - 273.15;
		if (gui_st.is_radiometric) {
			hse_t = (float) lep_info.lep_housing_temp_k100 / 100.0 - 273.15; 
			sprintf(&cam_info_buf[n], " - Temp: FPA %2.1f C,  Housing %2.1f C\n", fpa_t, hse_t);
		} else {
			sprintf(&cam_info_buf[n], " - Temp: FPA %2.1f C\n", fpa_t);
		}
	} else {
		sprintf(&cam_info_buf[n], "\n");
	}
	
	return (strlen(cam_info_buf));
}


static int add_battery_info(int n)
{
	batt_status_t bs;
	
	// Get current values
	power_get_batt(&bs);
	
	sprintf(&cam_info_buf[n], "Battery: %1.2f V / %d mA  ", bs.batt_voltage, bs.load_ma);
	n = strlen(cam_info_buf);
	
	switch (bs.charge_state) {
		case CHARGE_OFF:
			sprintf(&cam_info_buf[n], "\n");
			break;
		case CHARGE_ON:
			sprintf(&cam_info_buf[n], "Charging - USB: %1.2f V / %d mA\n", bs.usb_voltage, bs.usb_ma);
			break;
		case CHARGE_DONE:
			sprintf(&cam_info_buf[n], "Charge Done - USB: %1.2f V / %d mA\n", bs.usb_voltage, bs.usb_ma);
			break;
		case CHARGE_FAULT:
			sprintf(&cam_info_buf[n], "Charge Fault - USB: %1.2f V / %d mA\n", bs.usb_voltage, bs.usb_ma);
			break;
	}
	
	return (strlen(cam_info_buf));
}


static int add_ip_address(int n)
{
	// Only display IP address if active (ap enabled or sta connected)
	if (( ap_mode && ((wifi_info->flags & WIFI_INFO_FLAG_ENABLED) != 0)) ||
	    (!ap_mode && ((wifi_info->flags & WIFI_INFO_FLAG_CONNECTED) != 0))) {
	    
		sprintf(&cam_info_buf[n], "IP Address: %d.%d.%d.%d\n", 
			    wifi_info->cur_ip_addr[3], wifi_info->cur_ip_addr[2],
			    wifi_info->cur_ip_addr[1], wifi_info->cur_ip_addr[0]);
	} else {
		sprintf(&cam_info_buf[n], "IP Address: - \n");
	}
	
	return (strlen(cam_info_buf));
}


static int add_mac_address(int n)
{
	uint8_t sys_mac_addr[6];
	
	esp_efuse_mac_get_default(sys_mac_addr);
	
	// Add 1 for soft AP mode (see "Miscellaneous System APIs" in the ESP-IDF documentation)
	if (ap_mode) sys_mac_addr[5] += 1;
	
	sprintf(&cam_info_buf[n], "MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
		sys_mac_addr[0], sys_mac_addr[1], sys_mac_addr[2],
		sys_mac_addr[3], sys_mac_addr[4], sys_mac_addr[5]);
	
	return (strlen(cam_info_buf));
}


static int add_storage_info(int n)
{
	int mb_tot;
	int mb_free;
	
	if (power_get_sdcard_present()) {
		mb_tot = (int) (file_get_storage_len() / (1000 * 1000));
		mb_free = (int) (file_get_storage_free() / (1000 * 1000));
		sprintf(&cam_info_buf[n], "Storage: %d MB used of %d MB - %d files\n",
			mb_tot - mb_free, mb_tot, file_get_num_files());
	} else {
		sprintf(&cam_info_buf[n], "Storage: No media\n");
	}
	
	return (strlen(cam_info_buf));
}


static int add_mem_info(int n)
{
	sprintf(&cam_info_buf[n], "Heap Free: Int %d (%d) - PSRAM %d (%d)\n",
		heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
		heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
		heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
		heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
		
	return (strlen(cam_info_buf));
}


static int add_fps(int n)
{
	sprintf(&cam_info_buf[n], "FPS: %1.1f\n", gui_st.fps);
	
	return (strlen(cam_info_buf));
}


static int add_copyright_info(int n)
{
	sprintf(&cam_info_buf[n], "\ntCam copyright (c) 2020-2022 Dan Julio.  All rights reserved.\n");
	
	return(strlen(cam_info_buf));
}


static void cb_btn_exit(lv_obj_t * btn, lv_event_t event)
{
	if (event == LV_EVENT_CLICKED) {
		gui_set_screen(GUI_SCREEN_SETTINGS);
	}
}


static void cb_reset_btn(lv_obj_t* btn, lv_event_t event)
{
	if (event == LV_EVENT_CLICKED) {
		if (!press_det_running) {
			// Register the first click
			press_det_running = true;
			press_det_count = 1;
			
			// Start our timer
			if (timer_task != NULL) {
				lv_task_del(timer_task);
				timer_task = NULL;
			}
			timer_task = lv_task_create(cb_timer, I_PRESS_DET_SECS * 1000, LV_TASK_PRIO_LOW, NULL);
		} else {
			if (++press_det_count == I_PRESS_DET_NUM) {
				// Disable our timer if is running
				lv_task_del(timer_task);
				timer_task = NULL;
				
				// Reset for next time
				press_det_running = false;
				press_det_count = 0;
				
				// Make sure the user wants to perform a factory reset - the button the user presses
				// on the messagebox will decide what we do in gui_screen_info_set_msgbox_btn
				gui_preset_message_box_string("Are you sure you want to perform a factory reset?", true);
				xTaskNotify(task_handle_gui, GUI_NOTIFY_MESSAGEBOX_MASK, eSetBits);
			}
		}
	}
}


static void cb_timer(lv_task_t * task)
{
	// Timeout: Disable checking for this series of presses
	press_det_running = false;
	press_det_count = 0;
	if (timer_task != NULL) {
		lv_task_del(timer_task);
		timer_task = NULL;
	}
}

