/*
 * Memory frame buffer for LVGL - allocated in PSRAM
 */
#ifndef MEM_FB_H_
#define MEM_FB_H_
#include "lv_conf.h"
#include "lvgl/lvgl.h"
#include "system_config.h"


// Dimensions
#define MEM_FB_W LV_HOR_RES_MAX
#define MEM_FB_H LV_VER_RES_MAX

// Bits per pixel (supports 8, 16 or 32)
#define MEM_FB_BPP LV_COLOR_DEPTH


// API
void mem_fb_init();
void mem_fb_flush(lv_disp_drv_t * drv, const lv_area_t * area, lv_color_t * color_map);
uint8_t* mem_fb_get_buffer();


#endif // MEM_FB_H_