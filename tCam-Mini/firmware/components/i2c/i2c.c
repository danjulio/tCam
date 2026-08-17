/*
 * I2C Module
 *
 * Provides I2C Access routines for other modules/tasks.  Provides a locking mechanism
 * since the underlying ESP IDF routines are not thread safe.
 *
 * Implemented on the driver/i2c_master API.  The old driver/i2c.h implementation
 * carried a latent quirk: transfers passed I2C_MODE_MASTER (== 1) where the port
 * number belongs, which only worked because I2C_MASTER_NUM also happened to be 1.
 * The new API's bus/device handles make that class of mistake unrepresentable.
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
#include "system_config.h"
#include "i2c.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"


//
// I2C constants
//

// Per-transfer timeout.  The Lepton's CCI can stall briefly during FFC, so this
// stays generous; the old implementation used the same figure.
#define I2C_XFER_TIMEOUT_MS 1000

// Distinct peripheral addresses this system talks to (Lepton CCI and, on boards
// that have one, the DS3232 RTC)
#define I2C_MAX_DEVICES 4


//
// I2C variables
//
static SemaphoreHandle_t i2c_mutex;

static i2c_master_bus_handle_t i2c_bus = NULL;

// The new driver wants one device handle per peripheral address.  Callers hand
// us raw addresses, so handles are created on first use and cached.
static i2c_master_dev_handle_t dev_handles[I2C_MAX_DEVICES];
static uint8_t dev_addrs[I2C_MAX_DEVICES];
static int dev_count = 0;



//
// I2C Forward declarations
//
static i2c_master_dev_handle_t get_device(uint8_t addr7);



//
// I2C API
//

/**
 * i2c master initialization
 */
esp_err_t i2c_master_init(int scl_pin, int sda_pin)
{
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_MASTER_NUM,
        .scl_io_num = scl_pin,
        .sda_io_num = sda_pin,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    // Create a mutex for thread safety
    i2c_mutex = xSemaphoreCreateMutex();

    return i2c_new_master_bus(&bus_config, &i2c_bus);
}


/**
 * i2c master lock
 */
void i2c_lock()
{
	xSemaphoreTake(i2c_mutex, portMAX_DELAY);
}


/**
 * i2c master unlock
 */
void i2c_unlock()
{
	xSemaphoreGive(i2c_mutex);
}


/**
 * Read from a peripheral (address + read, n bytes, stop)
 */
esp_err_t i2c_master_read_slave(uint8_t addr7, uint8_t *data_rd, size_t size)
{
    i2c_master_dev_handle_t dev;

    if (size == 0) {
        return ESP_OK;
    }

    dev = get_device(addr7);
    if (dev == NULL) {
        return ESP_ERR_NO_MEM;
    }

    return i2c_master_receive(dev, data_rd, size, I2C_XFER_TIMEOUT_MS);
}


/**
 * Write to a peripheral (address + write, n bytes, stop)
 */
esp_err_t i2c_master_write_slave(uint8_t addr7, uint8_t *data_wr, size_t size)
{
    i2c_master_dev_handle_t dev = get_device(addr7);

    if (dev == NULL) {
        return ESP_ERR_NO_MEM;
    }

    return i2c_master_transmit(dev, data_wr, size, I2C_XFER_TIMEOUT_MS);
}



//
// I2C internal functions
//

/**
 * Return (creating and caching on first use) the device handle for an address.
 * Called with the i2c lock held by convention, like every transfer.
 */
static i2c_master_dev_handle_t get_device(uint8_t addr7)
{
    esp_err_t ret;
    int i;
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr7,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };

    for (i = 0; i < dev_count; i++) {
        if (dev_addrs[i] == addr7) {
            return dev_handles[i];
        }
    }

    if (dev_count >= I2C_MAX_DEVICES) {
        return NULL;
    }

    ret = i2c_master_bus_add_device(i2c_bus, &dev_config, &dev_handles[dev_count]);
    if (ret != ESP_OK) {
        return NULL;
    }

    dev_addrs[dev_count] = addr7;
    return dev_handles[dev_count++];
}
