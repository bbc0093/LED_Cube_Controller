idf_component_register(
        SRCS "icm42688p.c"
        INCLUDE_DIRS "."
        PRIV_REQUIRES esp_driver_i2c freertos
)

target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall
        -Werror
)
