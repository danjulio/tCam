file(GLOB SOURCES *.c)

idf_component_register(SRCS ${SOURCES}
                       INCLUDE_DIRS . ../../i2c ../lvgl_tft ../../lvgl
                       REQUIRES lvgl)
