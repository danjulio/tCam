file(GLOB SOURCES *.c)

idf_component_register(SRCS ${SOURCES}
                       INCLUDE_DIRS . ../../main
                       REQUIRES cmd nvs_flash driver esp_wifi esp_netif esp_eth esp_event)
