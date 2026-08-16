/**
 * @file disp_spi.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "esp_system.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"

#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "lvgl/lvgl.h"

#include "disp_spi.h"
#include "disp_driver.h"


/*********************
 *      DEFINES
 *********************/


/**********************
 *      TYPEDEFS
 **********************/


/**********************
 *  STATIC PROTOTYPES
 **********************/
static void IRAM_ATTR spi_ready (spi_transaction_t *trans);


/**********************
 *  STATIC VARIABLES
 **********************/
static spi_device_handle_t spi;
static volatile bool spi_trans_in_progress = false;
static volatile bool spi_color_sent;


/**********************
 *      MACROS
 **********************/


/**********************
 *   GLOBAL FUNCTIONS
 **********************/
void disp_spi_add_device(spi_host_device_t host)
{
    spi_device_interface_config_t devcfg={
            .clock_speed_hz=DISP_SPI_HZ,
            .mode=LCD_SPI_MODE,
            .spics_io_num=DISP_SPI_CS,
            .queue_size=1,
            .pre_cb=NULL,
            .post_cb=spi_ready,
            .cs_ena_pretrans = 2,            // Make sure CS brackets transaction with enough
            .cs_ena_posttrans= 2,            //   time for setup/hold on serial-parallel circuit
            .flags = SPI_DEVICE_HALFDUPLEX
    };
    
    esp_err_t ret=spi_bus_add_device(host, &devcfg, &spi);
    assert(ret==ESP_OK);
}


void disp_spi_init(void)
{

    esp_err_t ret;

    spi_bus_config_t buscfg={
            .miso_io_num=-1,
            .mosi_io_num=DISP_SPI_MOSI,
            .sclk_io_num=DISP_SPI_CLK,
            .quadwp_io_num=-1,
            .quadhd_io_num=-1,
            .max_transfer_sz = DISP_BUF_SIZE * 2
    };

    //Initialize the SPI bus
    ret=spi_bus_initialize(DISP_SPI_HOST, &buscfg, 1);
    assert(ret==ESP_OK);

    //Attach the LCD to the SPI bus
    disp_spi_add_device(DISP_SPI_HOST);
}


void disp_spi_send_data(uint8_t * data, uint16_t length)
{
    if (length == 0) return;           //no need to send anything

    while (spi_trans_in_progress);

    spi_transaction_t t = {
        .length = length * 8, // transaction length is in bits
        .tx_buffer = data
    };

    spi_trans_in_progress = true;
    spi_color_sent = false;             //Mark the "lv_flush_ready" NOT needs to be called in "spi_ready"
    spi_device_queue_trans(spi, &t, portMAX_DELAY);
}


void disp_spi_send_colors(uint8_t * data, uint16_t length)
{
    if (length == 0) {
	return;
    }

    while(spi_trans_in_progress);

    spi_transaction_t t = {
        .length = length * 8, // transaction length is in bits
        .tx_buffer = data
    };
    
    spi_trans_in_progress = true;
    spi_color_sent = true;              //Mark the "lv_flush_ready" needs to be called in "spi_ready"
    spi_device_queue_trans(spi, &t, portMAX_DELAY);
}


bool disp_spi_is_busy(void)
{
    return spi_trans_in_progress;
}



/**********************
 *   STATIC FUNCTIONS
 **********************/

static void IRAM_ATTR spi_ready (spi_transaction_t *trans)
{
    spi_trans_in_progress = false;

    lv_disp_t * disp = lv_refr_get_disp_refreshing();
    if (spi_color_sent) lv_disp_flush_ready(&disp->driver);
}
