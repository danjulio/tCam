/*
 * gCore RTC Module
 *
 * Provides access to the gCore Real-Time clock and alarm.
 *
 * With from Michael Margolis' time.c file.
 *
 * Copyright 2021 Michael Margolis and Dan Julio
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

#ifndef GCORE_RTC_H
#define GCORE_RTC_H

#include <stdint.h>
#include "esp_system.h"
#include <time.h>
#include <sys/time.h>


//
// RTC constants
//

// Convenience macros to convert to and from tm years 
#define  tmYearToCalendar(Y) ((Y) + 1970)  // full four digit year 
#define  CalendarYrToTm(Y)   ((Y) - 1970)
#define  tmYearToY2k(Y)      ((Y) - 30)    // offset is from 2000
#define  y2kYearToTm(Y)      ((Y) + 30)


//
// Time structures
//
typedef struct  {
	uint16_t Millisecond; // 0 - 999
	uint8_t Second;       // 0 - 59 
	uint8_t Minute;       // 0 - 59
	uint8_t Hour;         // 0 - 23
	uint8_t Wday;         // 1 - 7; day of week, sunday is day 1
	uint8_t Day;          // 1 - 31
	uint8_t Month;        // 1 - 12
	uint8_t Year;         // offset from 1970; 
} tmElements_t;



//
// RTC API
//

time_t rtc_get_time_secs();
bool rtc_set_time_secs(time_t t);
void rtc_read_time(tmElements_t* tm);
bool rtc_write_time(const tmElements_t tm);

time_t rtc_get_alarm_secs();
bool rtc_set_alarm_secs(time_t t);
void rtc_read_alarm(tmElements_t* tm);
bool rtc_write_alarm(const tmElements_t tm);

bool rtc_enable_alarm(bool en);
bool rtc_get_alarm_enable(bool* en);

void rtc_breakTime(time_t timeInput, tmElements_t* tm);
time_t rtc_makeTime(const tmElements_t tm);



#endif /* GCORE_RTC_H */
