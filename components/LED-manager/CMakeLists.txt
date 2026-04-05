idf_component_register(
        SRCS "LED-manager.c" "LED-encoder.c"
        INCLUDE_DIRS "."
        REQUIRES esp_driver_rmt
)
