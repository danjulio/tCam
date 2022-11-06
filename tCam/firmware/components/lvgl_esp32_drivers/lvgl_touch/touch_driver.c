/**
 * @file touch_driver.c
 */
#include "touch_driver.h"
#include "ft6x36.h"


void touch_driver_init()
{
	ft6x36_init(FT6236_I2C_SLAVE_ADDR);
}


bool touch_driver_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    bool res = false;

	res = ft6x36_read(drv, data);

    return res;
}

