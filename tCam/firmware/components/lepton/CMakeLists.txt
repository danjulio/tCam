file(GLOB SOURCES *.c)

idf_component_register(SRCS ${SOURCES}
                       INCLUDE_DIRS . ../cmd ../gcore ../gui ../i2c ../lvgl ../../main ../sys
                       REQUIRES json)