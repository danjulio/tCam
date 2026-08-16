/**
 * @file disp_driver.c
 */

#include "disp_driver.h"
#include "disp_spi.h"
#include "ili9488.h"
#ifdef SYS_SCREENDUMP_ENABLE
#include "mem_fb.h"
#endif


#ifdef SYS_SCREENDUMP_ENABLE
static bool enable_dump;

void disp_driver_init(bool init_spi)
{
	if (init_spi) {
		disp_spi_init();
	}
	
	ili9488_init();
	
	mem_fb_init();
	
	enable_dump = false;
}

void disp_driver_flush(lv_disp_drv_t * drv, const lv_area_t * area, lv_color_t * color_map)
{
	if (enable_dump) {
		mem_fb_flush(drv, area, color_map);
	} else {
		ili9488_flush(drv, area, color_map);
	}
}

void disp_driver_en_dump(bool en_dump)
{
	enable_dump = en_dump;
}
#else
void disp_driver_init(bool init_spi)
{
	if (init_spi) {
		disp_spi_init();
	}

	ili9488_init();
}

void disp_driver_flush(lv_disp_drv_t * drv, const lv_area_t * area, lv_color_t * color_map)
{
	ili9488_flush(drv, area, color_map);
}
#endif
