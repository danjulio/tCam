set(SOURCES main.c ble_beacon.c client_if.c ctrl_task.c dns_hijack.c lep_task.c log_ring.c
            mon_task.c net_cmd_task.c rsp_task.c sif_cmd_task.c web_cmd.c web_task.c)

# Compress the web UI and draw the app icons before they are embedded.
# index.html is the source of truth and is the only one of these under version
# control.  The generated files go to the build directory, NOT back into the
# source tree: zlib output differs slightly between machines, so writing them
# alongside the source left every build with a dirty working tree and a pull
# that refused to merge.  Both scripts rewrite only when the bytes change, so an
# untouched UI does not force a relink.
set(WEB_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/www)

# Skipped during CMake's early requirements-expansion pass, where this file is
# processed from a different directory and the generated paths are not yet
# meaningful.  CMAKE_CURRENT_LIST_DIR - not CMAKE_CURRENT_SOURCE_DIR - is the
# one that reliably names this component in both passes.
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    file(MAKE_DIRECTORY ${WEB_GEN_DIR})

    execute_process(
        COMMAND ${CMAKE_COMMAND} -E echo "Compressing web UI"
        RESULT_VARIABLE _ignored
    )
    find_package(Python3 COMPONENTS Interpreter QUIET REQUIRED)
    execute_process(
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/compress_web.py
                ${CMAKE_CURRENT_LIST_DIR}/www/index.html
                ${WEB_GEN_DIR}/index.html.gz
        RESULT_VARIABLE WEB_GZ_RESULT
    )
    if(NOT WEB_GZ_RESULT EQUAL 0)
        message(FATAL_ERROR "Could not compress the web UI - the firmware embeds it, "
                            "so there is nothing sensible to fall back to")
    endif()

    execute_process(
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/make_icons.py ${WEB_GEN_DIR}
        RESULT_VARIABLE WEB_ICON_RESULT
    )
    if(NOT WEB_ICON_RESULT EQUAL 0)
        message(FATAL_ERROR "Could not generate the web app icons")
    endif()
endif()

idf_component_register(SRCS ${SOURCES}
                    INCLUDE_DIRS .
                    REQUIRES clock cmd i2c lepton sys app_update bt esp_http_server
                             esp_wifi esp_netif esp_timer nvs_flash mdns driver
                    EMBED_FILES "${WEB_GEN_DIR}/index.html.gz"
                                "${WEB_GEN_DIR}/icon-192.png"
                                "${WEB_GEN_DIR}/icon-512.png")
