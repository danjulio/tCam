/**
 * @file disp_spi.h
 *
 */

#ifndef DISP_SPI_H
#define DISP_SPI_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include <stdint.h>
#include <stdbool.h>
#include <driver/spi_master.h>
#include "lv_conf.h"
#include "system_config.h"



/*********************
 *      DEFINES
 *********************/
 
// SPI Bus when using disp_spi_init()
 #define DISP_SPI_HOST LCD_SPI_HOST

// Buffer size - sets maximum update region (and can use a lot of memory!)
#define DISP_BUF_SIZE LVGL_DISP_BUF_SIZE
 
// Display-specific GPIO
#define DISP_SPI_MOSI LCD_MOSI_IO
#define DISP_SPI_CLK  LCD_SCK_IO
#define DISP_SPI_CS   LCD_CSN_IO

// Display SPI frequency
#define DISP_SPI_HZ   LCD_SPI_FREQ_HZ


/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/
void disp_spi_init(void);
void disp_spi_add_device(spi_host_device_t host);
void disp_spi_send_data(uint8_t * data, uint16_t length);
void disp_spi_send_colors(uint8_t * data, uint16_t length);
bool disp_spi_is_busy(void);

/**********************
 *      MACROS
 **********************/


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*DISP_SPI_H*/
