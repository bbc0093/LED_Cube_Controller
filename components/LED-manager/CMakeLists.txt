idf_component_register(
        SRCS "LED-manager.c" "LED-encoder.c"
        INCLUDE_DIRS "."
        PRIV_REQUIRES esp_driver_gpio esp_driver_rmt
)

target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall
        -Werror
)