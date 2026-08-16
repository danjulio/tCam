set(SOURCES main.c app_task.c cmd_task.c file_task.c gcore_task.c gui_task.c lep_task.c mon_task.c rsp_task.c)
idf_component_register(SRCS ${SOURCES}
                    INCLUDE_DIRS . ../components/gcore
                    REQUIRES cmd gcore gui i2c lepton lvgl lvgl_esp32_drivers mdns sys)

target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_CONF_INCLUDE_SIMPLE=1)