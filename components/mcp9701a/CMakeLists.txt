idf_component_register(
        SRCS "mcp9701a.c"
        INCLUDE_DIRS "."
        PRIV_REQUIRES esp_adc freertos
)

target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall
        -Werror
)