file(GLOB SOURCES *.c)

idf_component_register(SRCS ${SOURCES}
                       INCLUDE_DIRS . ../gcore ../gui ../lepton ../lvgl ../../main ../sys
                       REQUIRES json app_update mbedtls)