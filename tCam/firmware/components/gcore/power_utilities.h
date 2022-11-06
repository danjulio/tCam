/*
 * Power related utilities
 *
 * Contains functions to read battery, charge and power button information in a
 * thread-safe way for gcore_task and others.
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
#ifndef POWER_UTILITIES_H
#define POWER_UTILITIES_H

#include <stdbool.h>
#include <stdint.h>


//
// Power Utilities constants
//

// Power button press duration for power-on/off
#define POWER_BUTTON_DUR_MSEC 1000

// Averaging sample counts
#define BATT_NUM_AVG_SAMPLES  16
#define POWER_AUX_AVG_SAMPLES 8

// Battery state-of-charge curve
//   Based on 0.2C discharge rate on https://www.richtek.com/battery-management/en/designing-liion.html
//   This isn't particularly accurate...
#define BATT_75_THRESHOLD     3.9
#define BATT_50_THRESHOLD     3.72
#define BATT_25_THRESHOLD     3.66
#define BATT_0_THRESHOLD      3.55
#define BATT_CRIT_THRESHOLD   3.4


//
// Battery status data structures
//
enum BATT_STATE_t {
	BATT_100,
	BATT_75,
	BATT_50,
	BATT_25,
	BATT_0,
	BATT_CRIT
};

enum CHARGE_STATE_t {
	CHARGE_OFF,
	CHARGE_ON,
	CHARGE_DONE,
	CHARGE_FAULT
};

typedef struct {
	float batt_voltage;
	float usb_voltage;
	uint16_t load_ma;
	uint16_t usb_ma;
	enum BATT_STATE_t batt_state;
	enum CHARGE_STATE_t charge_state;
} batt_status_t;



//
// Power Utilities API
//
bool power_init();
void power_set_brightness(int percent);
void power_batt_update();
void power_status_update();
void power_get_batt(batt_status_t* bs);
bool power_button_pressed();
bool power_get_sdcard_present();
void power_off();

#endif /* POWER_UTILITIES_H */