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
#include "esp_system.h"
#include "esp_log.h"
#include "gcore.h"
#include "rtc.h"


//
// RTC private constants
//

#ifndef _BV
#define _BV(bit) (1 << (bit))
#endif

// Leap year calulator expects year argument as years offset from 1970
#define LEAP_YEAR(Y)     ( ((1970+(Y))>0) && !((1970+(Y))%4) && ( ((1970+(Y))%100) || !((1970+(Y))%400) ) )

// Useful time constants
#define SECS_PER_MIN  ((time_t)(60UL))
#define SECS_PER_HOUR ((time_t)(3600UL))
#define SECS_PER_DAY  ((time_t)(SECS_PER_HOUR * 24UL))
#define DAYS_PER_WEEK ((time_t)(7UL))
#define SECS_PER_WEEK ((time_t)(SECS_PER_DAY * DAYS_PER_WEEK))
#define SECS_PER_YEAR ((time_t)(SECS_PER_DAY * 365UL)) // TODO: ought to handle leap years
#define SECS_YR_2000  ((time_t)(946684800UL)) // the time at the start of y2k



//
// RTC variables
//
static const char* TAG = "RTC";

static const uint8_t monthDays[]={31,28,31,30,31,30,31,31,30,31,30,31}; // API starts months from 1, this array starts from 0



//
// RTC API
//

time_t rtc_get_time_secs()
{
    uint32_t t = 0;
    
    (void) gcore_get_time_secs(&t);
    
    return((time_t) t);
}


bool rtc_set_time_secs(time_t t)
{
    return (gcore_set_time_secs((uint32_t) t));
}


void rtc_read_time(tmElements_t* tm)
{
	uint32_t t = 0;
	
	(void) rtc_get_time_secs(&t);
	
	rtc_breakTime(t, tm);
}


bool rtc_write_time(const tmElements_t tm)
{
	return (gcore_set_time_secs((uint32_t) rtc_makeTime(tm)) == true);
}


time_t rtc_get_alarm_secs()
{
	uint32_t t = 0;
    
    (void) gcore_get_alarm_secs(&t);
    
    return((time_t) t);
}


bool rtc_set_alarm_secs(time_t t)
{
	return (gcore_set_alarm_secs((uint32_t) t));
}


void rtc_read_alarm(tmElements_t* tm)
{
	uint32_t t = 0;
	
	(void) gcore_get_alarm_secs(&t);
	
	rtc_breakTime(t, tm);
}


bool rtc_write_alarm(tmElements_t tm)
{
	return (gcore_set_alarm_secs((uint32_t) rtc_makeTime(tm)) == true);
}


bool rtc_enable_alarm(bool en)
{
	
	return (gcore_set_wakeup_bit(GCORE_WK_ALARM_MASK, en) == true);
}


bool rtc_get_alarm_enable(bool* en)
{
	uint8_t t8;
	
	if (gcore_get_reg8(GCORE_REG_WK_CTRL, &t8)) {
		*en = ((t8 & GCORE_WK_ALARM_MASK) == GCORE_WK_ALARM_MASK);
		return true;
	} else {
		return false;
	}
}


/**
 * Break the given time_t into time components.
 * This is a more compact version of the C library localtime function.
 * Note that year is offset from 1970.
 */
void rtc_breakTime(time_t timeInput, tmElements_t* tm){
	uint8_t year;
	uint8_t month, monthLength;
	uint32_t time;
	unsigned long days;

	time = (uint32_t)timeInput;
	tm->Millisecond = 0;
	tm->Second = time % 60;
	time /= 60; // now it is minutes
	tm->Minute = time % 60;
	time /= 60; // now it is hours
	tm->Hour = time % 24;
	time /= 24; // now it is days
	tm->Wday = ((time + 4) % 7) + 1;  // Sunday is day 1 
  
	year = 0;  
	days = 0;
	while((unsigned)(days += (LEAP_YEAR(year) ? 366 : 365)) <= time) {
		year++;
	}
	tm->Year = year; // year is offset from 1970 
  
	days -= LEAP_YEAR(year) ? 366 : 365;
	time  -= days; // now it is days in this year, starting at 0
  
	days=0;
	month=0;
	monthLength=0;
	for (month=0; month<12; month++) {
		if (month==1) { // february
			if (LEAP_YEAR(year)) {
				monthLength=29;
			} else {
				monthLength=28;
			}
		} else {
			monthLength = monthDays[month];
		}
    
		if (time >= monthLength) {
			time -= monthLength;
		} else {
			break;
		}
	}
	tm->Month = month + 1;  // jan is month 1  
	tm->Day = time + 1;     // day of month
}


/**
 * Assemble time elements into time_t seconds.
 * Note year argument is offset from 1970
 */
time_t rtc_makeTime(const tmElements_t tm){  
	int i;
	uint32_t seconds;

	// seconds from 1970 till 1 jan 00:00:00 of the given year
	seconds= tm.Year*(SECS_PER_DAY * 365);
	for (i = 0; i < tm.Year; i++) {
		if (LEAP_YEAR(i)) {
			seconds +=  SECS_PER_DAY;   // add extra days for leap years
		}
	}
  
	// add days for this year, months start from 1
	for (i = 1; i < tm.Month; i++) {
		if ( (i == 2) && LEAP_YEAR(tm.Year)) { 
			seconds += SECS_PER_DAY * 29;
		} else {
			seconds += SECS_PER_DAY * monthDays[i-1];  //monthDay array starts from 0
		}
	}
	seconds+= (tm.Day-1) * SECS_PER_DAY;
	seconds+= tm.Hour * SECS_PER_HOUR;
	seconds+= tm.Minute * SECS_PER_MIN;
	seconds+= tm.Second;
	return (time_t)seconds; 
}