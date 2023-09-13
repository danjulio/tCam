file(GLOB SOURCES *.c)

idf_component_register(SRCS ${SOURCES}
                       INCLUDE_DIRS . ../cmd ../lepton ../lvgl ../../main ../gcore palettes ../sys
                       REQUIRES app_update)