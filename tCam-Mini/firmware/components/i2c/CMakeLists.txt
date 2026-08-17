file(GLOB SOURCES *.c)

# IDF v5 dropped 'driver' from the common component requirements, so every
# component that includes a driver/ header has to ask for it by name.
idf_component_register(SRCS ${SOURCES}
                       INCLUDE_DIRS . ../../main
                       REQUIRES esp_driver_i2c)
