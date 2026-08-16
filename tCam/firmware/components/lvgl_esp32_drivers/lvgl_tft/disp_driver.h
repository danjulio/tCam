/**
 * @file disp_driver.h
 */

#ifndef DISP_DRIVER_H
#define DISP_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include <stdbool.h>
#include "lvgl/lvgl.h"
#include "system_config.h"


/*********************
 *      DEFINES
 *********************/
 

/**********************
 *      TYPEDEFS
 **********************/
 

/**********************
 * GLOBAL PROTOTYPES
 **********************/
void disp_driver_init(bool init_spi);
void disp_driver_flush(lv_disp_drv_t * drv, const lv_area_t * area, lv_color_t * color_map);
#ifdef SYS_SCREENDUMP_ENABLE
void disp_driver_en_dump(bool en_dump);
#endif


/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*DISP_DRIVER_H*/
