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
#include "esp_system.h"
#include "esp_log.h"
#include "gcore.h"
#include "i2c.h"


//
// gCore Variables
//
static const char* TAG = "gCore";



//
// gCore API
//
bool gcore_get_reg8(uint8_t offset, uint8_t* dat)
{
	uint8_t buf[2];
	uint16_t reg_addr;
	
	if (offset >= GCORE_REG_LEN) {
		ESP_LOGE(TAG, "REG offset 0x%0x too large", offset);
		return false;
	}
	
	reg_addr = GCORE_REG_BASE + offset;
	buf[0] = reg_addr >> 8;
	buf[1] = reg_addr & 0xFF;
	
	i2c_lock();
	
	// Write the register address
	if (i2c_master_write_slave(GCORE_I2C_ADDR, buf, 2) != ESP_OK) {
		i2c_unlock();
		ESP_LOGE(TAG, "failed to write byte register address 0x%02x", offset);
		return false;
	}

	// Read the register
	if (i2c_master_read_slave(GCORE_I2C_ADDR, buf, 1) != ESP_OK) {
		i2c_unlock();
		ESP_LOGE(TAG, "failed to read from byte register 0x%02x", offset);
		return false;
	}
	
	i2c_unlock();

	*dat = buf[0];
	return true;
}


bool gcore_set_reg8(uint8_t offset, uint8_t dat)
{
	uint8_t buf[3];
	uint16_t reg_addr;
	
	if (offset >= GCORE_REG_LEN) {
		ESP_LOGE(TAG, "REG offset 0x%0x too large", offset);
		return false;
	}
	
	reg_addr = GCORE_REG_BASE + offset;
	buf[0] = reg_addr >> 8;
	buf[1] = reg_addr & 0xFF;
	buf[2] = dat;
	
	i2c_lock();
	
	// Write the address + data
	if (i2c_master_write_slave(GCORE_I2C_ADDR, buf, 3) != ESP_OK) {
		i2c_unlock();
		ESP_LOGE(TAG, "failed to write byte register 0x%02x = 0x%2x", offset, dat);
		return false;
	}

	i2c_unlock();

	return true;
}


bool gcore_get_reg16(uint8_t offset, uint16_t* dat)
{
	uint8_t buf[2];
	uint16_t reg_addr;
	
	if (offset >= GCORE_REG_LEN) {
		ESP_LOGE(TAG, "REG offset 0x%0x too large", offset);
		return false;
	}
	
	reg_addr = GCORE_REG_BASE + offset;
	buf[0] = reg_addr >> 8;
	buf[1] = reg_addr & 0xFF;
	
	i2c_lock();
	
	// Write the register address
	if (i2c_master_write_slave(GCORE_I2C_ADDR, buf, 2) != ESP_OK) {
		i2c_unlock();
		ESP_LOGE(TAG, "failed to write word register address 0x%02x", offset);
		return false;
	}

	// Read the register
	if (i2c_master_read_slave(GCORE_I2C_ADDR, buf, 2) != ESP_OK) {
		i2c_unlock();
		ESP_LOGE(TAG, "failed to read from word register 0x%02x", offset);
		return false;
	}
	
	i2c_unlock();

	*dat = (buf[0] << 8) | buf[1];
	return true;
}


bool gcore_set_reg16(uint8_t offset, uint16_t dat)
{
	uint8_t buf[4];
	uint16_t reg_addr;
	
	if (offset >= GCORE_REG_LEN) {
		ESP_LOGE(TAG, "REG offset 0x%0x too large", offset);
		return false;
	}
	
	reg_addr = GCORE_REG_BASE + offset;
	buf[0] = reg_addr >> 8;
	buf[1] = reg_addr & 0xFF;
	buf[2] = dat >> 8;
	buf[3] = dat & 0xFF;
	
	i2c_lock();
	
	// Write the address + data
	if (i2c_master_write_slave(GCORE_I2C_ADDR, buf, 4) != ESP_OK) {
		i2c_unlock();
		ESP_LOGE(TAG, "failed to write word register 0x%02x = 0x%04x", offset, dat);
		return false;
	}

	i2c_unlock();

	return true;
}


bool gcore_set_wakeup_bit(uint8_t mask, bool en)
{
	uint8_t buf[3];
	uint16_t reg_addr;
	
	reg_addr = GCORE_REG_BASE + GCORE_REG_WK_CTRL;
	buf[0] = reg_addr >> 8;
	buf[1] = reg_addr & 0xFF;
	buf[2] = 0;
	
	// Perform a read-modify-write
	i2c_lock();
	
	// Write the register address
	if (i2c_master_write_slave(GCORE_I2C_ADDR, buf, 2) != ESP_OK) {
		i2c_unlock();
		ESP_LOGE(TAG, "failed to write WK_CTRL address");
		return false;
	}

	// Read the register
	if (i2c_master_read_slave(GCORE_I2C_ADDR, buf, 1) != ESP_OK) {
		i2c_unlock();
		ESP_LOGE(TAG, "failed to read WK_CTRL");
		return false;
	}
	
	// Write the modified value back
	if (en) {
		buf[2] = buf[0] | mask;
	} else {
		buf[2] = buf[0] & ~mask;
	}
	buf[0] = reg_addr >> 8;
	buf[1] = reg_addr & 0xFF;
	if (i2c_master_write_slave(GCORE_I2C_ADDR, buf, 3) != ESP_OK) {
		i2c_unlock();
		ESP_LOGE(TAG, "failed to write WK_CTRL");
		return false;
	}
	
	i2c_unlock();

	return true;
}


bool gcore_get_nvram_byte(uint16_t offset, uint8_t* dat)
{
	uint8_t buf[2];
	uint16_t reg_addr;
	
	if (offset >= GCORE_NVRAM_FULL_LEN) {
		ESP_LOGE(TAG, "NVRAM offset 0x%04x too large", offset);
		return false;
	}
	
	reg_addr = GCORE_NVRAM_BASE + offset;
	buf[0] = reg_addr >> 8;
	buf[1] = reg_addr & 0xFF;
	
	i2c_lock();
	
	// Write the register address
	if (i2c_master_write_slave(GCORE_I2C_ADDR, buf, 2) != ESP_OK) {
		i2c_unlock();
		ESP_LOGE(TAG, "failed to write byte register address 0x%02x", offset);
		return false;
	}

	// Read the register
	if (i2c_master_read_slave(GCORE_I2C_ADDR, buf, 1) != ESP_OK) {
		i2c_unlock();
		ESP_LOGE(TAG, "failed to read from byte register 0x%02x", offset);
		return false;
	}
	
	i2c_unlock();

	*dat = buf[0];
	return true;
}


bool gcore_set_nvram_byte(uint16_t offset, uint8_t dat)
{
	uint8_t buf[3];
	uint16_t reg_addr;
	
	if (offset >= GCORE_NVRAM_FULL_LEN) {
		ESP_LOGE(TAG, "NVRAM offset 0x%04x too large", offset);
		return false;
	}
	
	reg_addr = GCORE_NVRAM_BASE + offset;
	buf[0] = reg_addr >> 8;
	buf[1] = reg_addr & 0xFF;
	buf[2] = dat;
	
	i2c_lock();
	
	// Write the address + data
	if (i2c_master_write_slave(GCORE_I2C_ADDR, buf, 3) != ESP_OK) {
		i2c_unlock();
		ESP_LOGE(TAG, "failed to write NVRAM 0x%02x = 0x%2x", offset, dat);
		return false;
	}

	i2c_unlock();

	return true;
}


bool gcore_get_nvram_bytes(uint16_t offset, uint8_t* dat, uint16_t len)
{
	uint8_t buf[2];
	uint16_t reg_addr;
	
	if ((offset+len) > GCORE_NVRAM_FULL_LEN) {
		ESP_LOGE(TAG, "NVRAM offset 0x%04x too large", offset);
		return false;
	}
	
	reg_addr = GCORE_NVRAM_BASE + offset;
	buf[0] = reg_addr >> 8;
	buf[1] = reg_addr & 0xFF;
	
	i2c_lock();
	
	// Write the register address
	if (i2c_master_write_slave(GCORE_I2C_ADDR, buf, 2) != ESP_OK) {
		i2c_unlock();
		ESP_LOGE(TAG, "failed to write NVRAM register address 0x%04x", offset);
		return false;
	}

	// Read the bytes
	if (i2c_master_read_slave(GCORE_I2C_ADDR, dat, len) != ESP_OK) {
		i2c_unlock();
		ESP_LOGE(TAG, "failed to read %d bytes from NVRAM 0x%04x", len, offset);
		return false;
	}
	
	i2c_unlock();

	return true;
}


bool gcore_set_nvram_bytes(uint16_t offset, uint8_t* dat, uint16_t len)
{
	int i;
	uint8_t* buf;
	uint16_t reg_addr;
	
	if ((offset+len) > GCORE_NVRAM_FULL_LEN) {
		ESP_LOGE(TAG, "NVRAM offset 0x%04x too large", offset);
		return false;
	}
	
	buf = malloc(len+2);
	if (buf == NULL) {
		ESP_LOGE(TAG, "Could not malloc %d bytes", len+2);
		return false;
	}
	
	reg_addr = GCORE_NVRAM_BASE + offset;
	buf[0] = reg_addr >> 8;
	buf[1] = reg_addr & 0xFF;
	for (i=0; i<len; i++) {
		buf[i+2] = dat[i];
	}
	
	i2c_lock();
	
	// Write the address + data
	if (i2c_master_write_slave(GCORE_I2C_ADDR, buf, len+2) != ESP_OK) {
		i2c_unlock();
		free(buf);
		ESP_LOGE(TAG, "failed to write %d bytes to NVRAM 0x%04x", len, offset);
		return false;
	}

	i2c_unlock();
	
	free(buf);

	return true;
}


bool gcore_get_time_secs(uint32_t* s)
{
	uint8_t buf[4];
	uint16_t reg_addr;
	
	reg_addr = GCORE_REG_BASE + GCORE_REG_TIME;
	buf[0] = reg_addr >> 8;
	buf[1] = reg_addr & 0xFF;
	
	i2c_lock();
	
	// Write the register address
	if (i2c_master_write_slave(GCORE_I2C_ADDR, buf, 2) != ESP_OK) {
		i2c_unlock();
		ESP_LOGE(TAG, "failed to write TIME address");
		return false;
	}

	// Read the register
	if (i2c_master_read_slave(GCORE_I2C_ADDR, buf, 4) != ESP_OK) {
		i2c_unlock();
		ESP_LOGE(TAG, "failed to read TIME register");
		return false;
	}
	
	i2c_unlock();

	*s = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
	
	return true;
}


bool gcore_set_time_secs(uint32_t s)
{
	uint8_t buf[6];
	uint16_t reg_addr;
	
	reg_addr = GCORE_REG_BASE + GCORE_REG_TIME;
	buf[0] = reg_addr >> 8;
	buf[1] = reg_addr & 0xFF;
	buf[2] = (s >> 24) & 0xFF;
	buf[3] = (s >> 16) & 0xFF;
	buf[4] = (s >> 8) & 0xFF;
	buf[5] = s & 0xFF;
		
	i2c_lock();
	
	// Write the address + data
	if (i2c_master_write_slave(GCORE_I2C_ADDR, buf, 6) != ESP_OK) {
		i2c_unlock();
		ESP_LOGE(TAG, "failed to write TIME register");
		return false;
	}

	i2c_unlock();
	
	return true;
}


bool gcore_get_alarm_secs(uint32_t* s)
{
	uint8_t buf[4];
	uint16_t reg_addr;
	
	reg_addr = GCORE_REG_BASE + GCORE_REG_ALARM;
	buf[0] = reg_addr >> 8;
	buf[1] = reg_addr & 0xFF;
	
	i2c_lock();
	
	// Write the register address
	if (i2c_master_write_slave(GCORE_I2C_ADDR, buf, 2) != ESP_OK) {
		i2c_unlock();
		ESP_LOGE(TAG, "failed to write ALARM address");
		return false;
	}

	// Read the register
	if (i2c_master_read_slave(GCORE_I2C_ADDR, buf, 4) != ESP_OK) {
		i2c_unlock();
		ESP_LOGE(TAG, "failed to read ALARM register");
		return false;
	}
	
	i2c_unlock();

	*s = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
	return true;
}


bool gcore_set_alarm_secs(uint32_t s)
{
	uint8_t buf[6];
	uint16_t reg_addr;
	
	reg_addr = GCORE_REG_BASE + GCORE_REG_ALARM;
	buf[0] = reg_addr >> 8;
	buf[1] = reg_addr & 0xFF;
	buf[2] = (s >> 24) & 0xFF;
	buf[3] = (s >> 16) & 0xFF;
	buf[4] = (s >> 8) & 0xFF;
	buf[5] = s & 0xFF;
	
	i2c_lock();
	
	// Write the address + data
	if (i2c_master_write_slave(GCORE_I2C_ADDR, buf, 6) != ESP_OK) {
		i2c_unlock();
		ESP_LOGE(TAG, "failed to write ALARM register");
		return false;
	}

	i2c_unlock();

	return true;
}


bool gcore_get_corr_secs(uint32_t* s)
{
	uint8_t buf[4];
	uint16_t reg_addr;
	
	reg_addr = GCORE_REG_BASE + GCORE_REG_CORR;
	buf[0] = reg_addr >> 8;
	buf[1] = reg_addr & 0xFF;
	
	i2c_lock();
	
	// Write the register address
	if (i2c_master_write_slave(GCORE_I2C_ADDR, buf, 2) != ESP_OK) {
		i2c_unlock();
		ESP_LOGE(TAG, "failed to write Time Correction address");
		return false;
	}

	// Read the register
	if (i2c_master_read_slave(GCORE_I2C_ADDR, buf, 4) != ESP_OK) {
		i2c_unlock();
		ESP_LOGE(TAG, "failed to read Time Correction register");
		return false;
	}
	
	i2c_unlock();

	*s = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
	return true;
}


bool gcore_set_corr_secs(uint32_t s)
{
	uint8_t buf[6];
	uint16_t reg_addr;
	
	reg_addr = GCORE_REG_BASE + GCORE_REG_CORR;
	buf[0] = reg_addr >> 8;
	buf[1] = reg_addr & 0xFF;
	buf[2] = (s >> 24) & 0xFF;
	buf[3] = (s >> 16) & 0xFF;
	buf[4] = (s >> 8) & 0xFF;
	buf[5] = s & 0xFF;
	
	i2c_lock();
	
	// Write the address + data
	if (i2c_master_write_slave(GCORE_I2C_ADDR, buf, 6) != ESP_OK) {
		i2c_unlock();
		ESP_LOGE(TAG, "failed to write Time Correction register");
		return false;
	}

	i2c_unlock();

	return true;
}