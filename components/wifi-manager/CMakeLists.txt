idf_component_register(
        SRCS "wifi-manager.c"
        INCLUDE_DIRS "."
        REQUIRES esp_wifi esp_event esp_netif lwip
)

target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall
        -Werror
)