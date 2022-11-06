/*
 * tCam Main
 *
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
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "app_task.h"
#include "cmd_task.h"
#include "file_task.h"
#include "gcore_task.h"
#include "gui_task.h"
#include "lep_task.h"
#include "mon_task.h"
#include "rsp_task.h"
#include "system_config.h"
#include "sys_utilities.h"



static const char* TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "tCam starting");
    
    // Initialize the ESP32 IO pins, set PWR_EN to keep us powered up and initialize
    // the shared SPI and I2C drivers
    if (!system_esp_io_init()) {
    	ESP_LOGE(TAG, "tCam ESP32 init failed - shutting off");
    	system_shutoff();
    }
    
    // Initialize the camera's peripheral devices: RTC, persistent storage, Wifi, file system
    if (!system_peripheral_init()) {
    	ESP_LOGE(TAG, "tCam Peripheral init failed - shutting off");
    	system_shutoff();
    }
    
    // Pre-allocate big buffers
    if (!system_buffer_init()) {
    	ESP_LOGE(TAG, "tCam memory allocate failed - shutting off");
    	system_shutoff();
    }
    
    // Start tasks
    //   Core 0 : PRO
    //   Core 1 : APP
	
	// Create app_task so its handle exists for all other tasks
	xTaskCreatePinnedToCore(&app_task,  "app_task",  2560, NULL, 1, &task_handle_app,  1);
	
    // Start the GUI next (initializing the display takes some time)
    xTaskCreatePinnedToCore(&gui_task,  "gui_task",  2560, NULL, 3, &task_handle_gui,  0);
    
    // Delay to allow tCam-Mini to come up
    vTaskDelay(pdMS_TO_TICKS(1250));
    
    // Start remaining tasks
    xTaskCreatePinnedToCore(&cmd_task,    "cmd_task",    3072, NULL, 2, &task_handle_cmd,    0);
    xTaskCreatePinnedToCore(&file_task,   "file_task",   3072, NULL, 3, &task_handle_file,   1);
    xTaskCreatePinnedToCore(&gcore_task,  "gcore_task",  2048, NULL, 1, &task_handle_gcore,  0);
    xTaskCreatePinnedToCore(&rsp_task,    "rsp_task",    3072, NULL, 2, &task_handle_rsp,    1);
    xTaskCreatePinnedToCore(&lep_task,    "lep_task",    2560, NULL, 3, &task_handle_lep,    1);
#ifdef INCLUDE_SYS_MON
	xTaskCreatePinnedToCore(&mon_task,    "mon_task",    2048, NULL, 1, &task_handle_mon,    1);
#endif
}
