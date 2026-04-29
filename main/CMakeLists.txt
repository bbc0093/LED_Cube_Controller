idf_component_register(
        SRCS "main.c"
        INCLUDE_DIRS "."
        PRIV_REQUIRES
            wifi-manager
            LED-manager
            icm42688p
            mcp9701a
            tmp102
            max17049
            bq25731
            nvs_flash
            esp_driver_i2c
            esp_driver_gpio
            esp_adc
)

target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall
        -Werror
)