/*
 * gCore Interface Module
 *
 * Provide routines to access the EFM8 controller on gCore providing access to the RTC,
 * NVRAM and control/status registers.  Uses the system's i2c module for thread-safe
 * access.
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
#ifndef GCORE_H
#define GCORE_H
#include <stdbool.h>
#include <stdint.h>

//
// gCore Constants
//

// I2C address
#define GCORE_I2C_ADDR    0x12

// Register address offsets
#define GCORE_NVRAM_BASE  0x0000
#define GCORE_REG_BASE    0x1000

// Region sizes
#define GCORE_NVRAM_FULL_LEN 0x1000
#define GCORE_NVRAM_BCKD_LEN 0x0400
#define GCORE_REG_LEN        0x1F

// 8-bit Register offsets
#define GCORE_REG_ID      0x00
#define GCORE_REG_VER     0x01
#define GCORE_REG_STATUS  0x02
#define GCORE_REG_GPIO    0x03
#define GCORE_REG_VU      0x04
#define GCORE_REG_IU      0x06
#define GCORE_REG_VB      0x08
#define GCORE_REG_IL      0x0A
#define GCORE_REG_TEMP    0x0C
#define GCORE_REG_BL      0x0E
#define GCORE_REG_WK_CTRL 0x0F
#define GCORE_REG_SHDOWN  0x10
#define GCORE_REG_PWR_TM  0x11
#define GCORE_REG_NV_CTRL 0x12
#define GCORE_REG_TIME    0x13
#define GCORE_REG_ALARM   0x17
#define GCORE_REG_CORR    0x1B


//
// gCore FW ID
//
#define GCORE_FW_ID       0x01


//
// Register bitmasks
//

// GPIO register masks
#define GCORE_GPIO_SD_CARD_MASK  0x08
#define GCORE_GPIO_PWR_BTN_MASK  0x04
#define GCORE_GPIO_CHG_MASK      0x03
#define GCORE_GPIO_CHG_1_MASK    0x02
#define GCORE_GPIO_CHG_0_MASK    0x01

// Status register masks
#define GCORE_ST_CRIT_BATT_MASK  0x80
#define GCORE_ST_PB_PRESS_MASK   0x10
#define GCORE_ST_PWR_ON_RSN_MASK 0x07

// Status power-on reason bit masks
#define GCORE_PWR_ON_BTN_MASK    0x01
#define GCORE_PWR_ON_ALARM_MASK  0x02
#define GCORE_PWR_ON_CHG_MASK    0x04

// NVRAM Flash register busy mask (RO)
#define GCORE_NVRAM_BUSY_MASK    0x01
#define GCORE_NVRAM_IDLE_MASK    0x00

// Wakeup Control register masks
#define GCORE_WK_ALARM_MASK      0x01
#define GCORE_WK_CHRG_START_MASK 0x02
#define GCORE_WK_CHRG_DONE_MASK  0x04


//
// Register special trigger values
//

// NVRAM Flash Register triggers (WO)
#define GCORE_NVRAM_WR_TRIG      'W'
#define GCORE_NVRAM_RD_TRIG      'R'

// Shutdown Register trigger (WO)
#define GCORE_SHUTDOWN_TRIG       0x0F


//
// Charge status bit values
//
#define GCORE_CHG_IDLE            0
#define GCORE_CHG_ACTIVE          1
#define GCORE_CHG_DONE            2
#define GCORE_CHG_FAULT           3


//
// gCore API
//
bool gcore_get_reg8(uint8_t offset, uint8_t* dat);
bool gcore_set_reg8(uint8_t offset, uint8_t dat);
bool gcore_get_reg16(uint8_t offset, uint16_t* dat);
bool gcore_set_reg16(uint8_t offset, uint16_t dat);

bool gcore_set_wakeup_bit(uint8_t mask, bool en);

bool gcore_get_nvram_byte(uint16_t offset, uint8_t* dat);
bool gcore_set_nvram_byte(uint16_t offset, uint8_t dat);
bool gcore_get_nvram_bytes(uint16_t offset, uint8_t* dat, uint16_t len);
bool gcore_set_nvram_bytes(uint16_t offset, uint8_t* dat, uint16_t len);

bool gcore_get_time_secs(uint32_t* s);
bool gcore_set_time_secs(uint32_t s);
bool gcore_get_alarm_secs(uint32_t* s);
bool gcore_set_alarm_secs(uint32_t s);
bool gcore_get_corr_secs(uint32_t* s);
bool gcore_set_corr_secs(uint32_t s);

#endif /* GCORE_H */