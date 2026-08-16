/*
* Copyright © 2020 Wolfgang Christl

* Permission is hereby granted, free of charge, to any person obtaining a copy of this 
* software and associated documentation files (the “Software”), to deal in the Software 
* without restriction, including without limitation the rights to use, copy, modify, merge, 
* publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons 
* to whom the Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all copies or 
* substantial portions of the Software.
* 
* THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, 
* INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR 
* PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE 
* FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, 
* ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE 
* SOFTWARE.
*/

#include <esp_log.h>
#include <driver/i2c.h>
#include <lvgl/lvgl.h>
#include "ft6x36.h"
#include "i2c.h"

#define TAG "FT6X36"


// Configuration constants
#define CONFIG_FT6X36_SWAPXY
#define CONFIG_FT6X36_INVERT_X
//#define CONFIG_FT6X36_INVERT_Y



// Global variables
ft6x36_status_t ft6x36_status;
uint8_t current_dev_addr;       // set during init



// Forward Declarations
static esp_err_t ft6x36_i2c_read8(uint8_t slave_addr, uint8_t register_addr, uint8_t *data_buf);
static esp_err_t ft6x36_i2c_read16(uint8_t slave_addr, uint8_t register_addr, uint16_t *data_buf);
static esp_err_t ft6x36_i2c_write8(uint8_t slave_addr, uint8_t register_addr, uint8_t data_buf);



// API

/**
  * @brief  Read the FT6x36 gesture ID. Initialize first!
  * @param  dev_addr: I2C FT6x36 Slave address.
  * @retval The gesture ID or 0x00 in case of failure
  */
uint8_t ft6x36_get_gesture_id() {
    if (!ft6x36_status.inited) {
        ESP_LOGE(TAG, "Init first!");
        return 0x00;
    }
    uint8_t data_buf;
    esp_err_t ret;
    if ((ret = ft6x36_i2c_read8(current_dev_addr, FT6X36_GEST_ID_REG, &data_buf) != ESP_OK))
        ESP_LOGE(TAG, "Error reading from device: %s", esp_err_to_name(ret));
    return data_buf;
}


/**
  * @brief  Initialize for FT6x36 communication via I2C
  * @param  dev_addr: Device address on communication Bus (I2C slave address of FT6X36).
  * @retval None
  */
void ft6x36_init(uint16_t dev_addr) {
	uint8_t data_buf;
	esp_err_t ret;
	
    if (!ft6x36_status.inited) {
        ft6x36_status.inited = true;
        current_dev_addr = dev_addr;
        
        ESP_LOGI(TAG, "Found touch panel controller");
        if ((ret = ft6x36_i2c_read8(dev_addr, FT6X36_PANEL_ID_REG, &data_buf) != ESP_OK))
            ESP_LOGE(TAG, "Error reading from device: %s",
                     esp_err_to_name(ret));    // Only show error the first time
        ESP_LOGI(TAG, "\tDevice ID: 0x%02x", data_buf);

        ft6x36_i2c_read8(dev_addr, FT6X36_CHIPSELECT_REG, &data_buf);
        ESP_LOGI(TAG, "\tChip ID: 0x%02x", data_buf);

        ft6x36_i2c_read8(dev_addr, FT6X36_DEV_MODE_REG, &data_buf);
        ESP_LOGI(TAG, "\tDevice mode: 0x%02x", data_buf);

        ft6x36_i2c_read8(dev_addr, FT6X36_FIRMWARE_ID_REG, &data_buf);
        ESP_LOGI(TAG, "\tFirmware ID: 0x%02x", data_buf);

        ft6x36_i2c_read8(dev_addr, FT6X36_RELEASECODE_REG, &data_buf);
        ESP_LOGI(TAG, "\tRelease code: 0x%02x", data_buf);
        
        ft6x36_i2c_write8(dev_addr, FT6X36_TH_GROUP_REG, FT62X36_DEFAULT_THRESHOLD);
    }
}

/**
  * @brief  Get the touch screen X and Y positions values. Ignores multi touch
  * @param  drv:
  * @param  data: Store data here
  * @retval Always false
  */
bool ft6x36_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
	esp_err_t ret;
    uint8_t touch_pnt_cnt;        // Number of detected touch points
    uint16_t cur_x;
    uint16_t cur_y;
    static int16_t last_x = 0;  // 12bit pixel value
    static int16_t last_y = 0;  // 12bit pixel value

    ret = ft6x36_i2c_read8(current_dev_addr, FT6X36_TD_STAT_REG, &touch_pnt_cnt);
    if (ret != ESP_OK) {
    	// There is an occasional failure of this read (perhaps from the FT6236 clock
    	// stretching) so we ignore failures (otherwise touch_pnt_cnt seems corrupted and
    	// we get an erroneous touch)
    	//ESP_LOGE(TAG, "Error getting STATUS register: %s", esp_err_to_name(ret));
        data->point.x = last_x;
        data->point.y = last_y;
        data->state = LV_INDEV_STATE_REL;   // no touch detected
        return false;
    }
    if (touch_pnt_cnt != 1) {    // ignore no touch & multi touch
        data->point.x = last_x;
        data->point.y = last_y;
        data->state = LV_INDEV_STATE_REL;
        return false;
    }
	
    // Read X value
    ret = ft6x36_i2c_read16(current_dev_addr, FT6X36_P1_XH_REG, &cur_x);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error getting X coordinates: %s", esp_err_to_name(ret));
        data->point.x = last_x;
        data->point.y = last_y;
        data->state = LV_INDEV_STATE_REL;   // no touch detected
        return false;
    }

    // Read Y value
    ret = ft6x36_i2c_read16(current_dev_addr, FT6X36_P1_YH_REG, &cur_y);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error getting Y coordinates: %s", esp_err_to_name(ret));
        data->point.x = last_x;
        data->point.y = last_y;
        data->state = LV_INDEV_STATE_REL;   // no touch detected
        return false;
    }

	last_x = cur_x & ((FT6X36_MSB_MASK << 8) | FT6X36_LSB_MASK);
	last_y = cur_y & ((FT6X36_MSB_MASK << 8) | FT6X36_LSB_MASK);

#ifdef CONFIG_FT6X36_SWAPXY
    int16_t swap_buf = last_x;
    last_x = last_y;
    last_y = swap_buf;
#endif
#ifdef CONFIG_FT6X36_INVERT_X
    last_x =  LV_HOR_RES - last_x;
#endif
#ifdef CONFIG_FT6X36_INVERT_Y
    last_y = LV_VER_RES - last_y;
#endif
    data->point.x = last_x;
    data->point.y = last_y;
    data->state = LV_INDEV_STATE_PR;
    //ESP_LOGV(TAG, "  X=%d Y=%d", data->point.x, data->point.y);
    //ESP_LOGI(TAG, "  X=%d Y=%d", data->point.x, data->point.y);
    return false;
}




// Internal Routines
/**
 * Read/write register routines for use below
 */
static esp_err_t ft6x36_i2c_read8(uint8_t slave_addr, uint8_t register_addr, uint8_t *data_buf) {
	uint8_t buf;
	esp_err_t ret;
	
	i2c_lock();
	
   // Write the register address
   	ret = i2c_master_write_slave(slave_addr, &register_addr, 1);
   	if (ret != ESP_OK) {
   		i2c_unlock();
   		return ret;
   	}

	// Read the register
	ret = i2c_master_read_slave(slave_addr, &buf, 1);
	if (ret != ESP_OK) {
		i2c_unlock();
		return ret;
	}
	
	i2c_unlock();

	*data_buf = buf;
    return ret;
}


static esp_err_t ft6x36_i2c_read16(uint8_t slave_addr, uint8_t register_addr, uint16_t *data_buf) {
	uint8_t buf[2];
	esp_err_t ret;
	
	i2c_lock();
	
   // Write the register address
   	ret = i2c_master_write_slave(slave_addr, &register_addr, 1);
   	if (ret != ESP_OK) {
   		i2c_unlock();
   		return ret;
   	}

	// Read the register
	ret = i2c_master_read_slave(slave_addr, buf, 2);
	if (ret != ESP_OK) {
		i2c_unlock();
		return ret;
	}
	
	i2c_unlock();

	*data_buf = (buf[0] << 8) | buf[1];
    return ret;
}


static esp_err_t ft6x36_i2c_write8(uint8_t slave_addr, uint8_t register_addr, uint8_t data_buf) {
	uint8_t buf[2];
	esp_err_t ret;
	
	buf[0] = register_addr;
	buf[1] = data_buf;
	
	i2c_lock();
	
   // Write the register
   	ret = i2c_master_write_slave(slave_addr, buf, 2);
   	if (ret != ESP_OK) {
   		i2c_unlock();
   		return ret;
   	}
	
	i2c_unlock();

    return ret;
}
