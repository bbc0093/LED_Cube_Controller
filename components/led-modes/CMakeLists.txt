idf_component_register(
        SRCS "led_modes.c" "mode_bst.c" "mode_imu_test.c"
        INCLUDE_DIRS "."
        REQUIRES LED-manager freertos esp_driver_gpio
)

target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall
        -Werror
)
