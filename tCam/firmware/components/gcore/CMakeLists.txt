file(GLOB SOURCES *.c)

idf_component_register(SRCS ${SOURCES}
                       INCLUDE_DIRS . ../cmd ../gui ../i2c ../../main ../lepton ../lvgl ../sys)