set(SOURCES main.c cert_utilities.c client_if.c ctrl_task.c dns_hijack.c lep_task.c mon_task.c
            net_cmd_task.c rsp_task.c sif_cmd_task.c web_cmd.c web_task.c)

# Compress the web UI before it is embedded.  index.html is the source of truth;
# index.html.gz is regenerated here on every configure so the two cannot drift.
# A pre-built .gz is also committed so the build still works if python is
# unavailable for any reason.
execute_process(
    COMMAND ${CMAKE_COMMAND} -E echo "Compressing web UI"
    RESULT_VARIABLE _ignored
)
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    execute_process(
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/compress_web.py
                ${CMAKE_CURRENT_SOURCE_DIR}/www/index.html
                ${CMAKE_CURRENT_SOURCE_DIR}/www/index.html.gz
        RESULT_VARIABLE WEB_GZ_RESULT
    )
    if(NOT WEB_GZ_RESULT EQUAL 0)
        message(WARNING "Could not compress web UI - using the committed index.html.gz")
    endif()
    # Web app icons.  Both scripts rewrite their output only when the bytes
    # change, so an untouched UI does not force a relink.
    execute_process(
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/make_icons.py
        RESULT_VARIABLE WEB_ICON_RESULT
    )
    if(NOT WEB_ICON_RESULT EQUAL 0)
        message(WARNING "Could not generate icons - using the committed PNGs")
    endif()
else()
    message(WARNING "Python not found - using the committed index.html.gz and icons")
endif()

idf_component_register(SRCS ${SOURCES}
                    INCLUDE_DIRS .
                    REQUIRES clock cmd i2c lepton sys app_update esp_http_server esp_https_server mbedtls nvs_flash mdns
                    EMBED_FILES "www/index.html.gz" "www/icon-192.png" "www/icon-512.png")

target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_CONF_INCLUDE_SIMPLE=1)
