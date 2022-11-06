/*
 * gCore Task
 *
 * Periodically updates operating state measured by gCore.  Detects snap picture button
 * press and shutdown conditions (power button press and critical battery) and notifies
 * the application task.
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
#include "app_task.h"
#include "gui_task.h"
#include "gcore_task.h"
#include "gcore.h"
#include "gui_utilities.h"
#include "power_utilities.h"
#include "sys_utilities.h"
#include "system_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include <stdbool.h>
#include <stdint.h>



//
// gCore Task variables
//
static const char* TAG = "gcore_task";

// State
static bool access_enable;



//
// gCore Task Forward Declarations for internal functions
//
static void gcore_task_handle_notifications();



//
// gCore Task API
//
void gcore_task()
{
	batt_status_t batt_status;
	int eval_count = GCORE_TASK_SAMPLE_MSEC / GCORE_TASK_EVAL_MSEC;

	ESP_LOGI(TAG, "Start task");
	
	access_enable = true;
	
	// Set the initial screen brightness
	power_set_brightness(gui_st.lcd_brightness);

	// Set the button for a short 50 mSec press detection
	(void) gcore_set_reg8(GCORE_REG_PWR_TM, 5);
	
	while (1) {
		// This task runs every GCORE_TASK_EVAL_MSEC mSec
		vTaskDelay(pdMS_TO_TICKS(GCORE_TASK_EVAL_MSEC));
		
		// Process notifications
		gcore_task_handle_notifications();
		
		if (access_enable) {		
			// Evaluate power button every evaluation
			power_status_update();
			if (power_button_pressed()) {
				xTaskNotify(task_handle_gui, GUI_NOTIFY_SNAP_BTN_PRESSED_MASK, eSetBits);
			}
			
			// Evaluate battery every GCORE_TASK_SAMPLE_MSEC mSec
			if (--eval_count == 0) {
				eval_count = GCORE_TASK_SAMPLE_MSEC / GCORE_TASK_EVAL_MSEC;
				
				// Update battery values
				power_batt_update();
				power_get_batt(&batt_status);
				
				if (batt_status.batt_state == BATT_CRIT) {
					ESP_LOGI(TAG, "Critical battery voltage detected");
					xTaskNotify(task_handle_app, APP_NOTIFY_SHUTDOWN_MASK, eSetBits);
				}
			}
		}
	}
}




//
// gCore Task internal functions
//

/**
 * Process notifications from other tasks
 */
static void gcore_task_handle_notifications()
{
	uint32_t notification_value = 0;
	
	// Handle notifications (clear them upon reading)
	if (xTaskNotifyWait(0x00, 0xFFFFFFFF, &notification_value, 0)) {
		//
		// SHUTDOWN
		//
		if (Notification(notification_value, GCORE_NOTIFY_SHUTDOWN_MASK)) {
			ESP_LOGI(TAG, "Initiate Power Off");
			system_shutoff();
			while (1) {
				vTaskDelay(pdMS_TO_TICKS(1000));
			}
		}
		
		//
		// ACCESS ENABLE - used to suppress accesses (typically when writing RAM
		// back to flash)
		//
		if (Notification(notification_value, GCORE_NOTIFY_HALT_ACCESS_MASK)) {
			access_enable = false;
		}
		if (Notification(notification_value, GCORE_NOTIFY_RESUME_ACCESS_MASK)) {
			access_enable = true;
		}
	}
}

