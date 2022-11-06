file(GLOB SOURCES *.c)

idf_component_register(SRCS ${SOURCES}
                       INCLUDE_DIRS . ../cmd ../gcore ../gui ../i2c ../lepton ../lvgl ../../main
                       REQUIRES app_update fatfs json nvs_flash)