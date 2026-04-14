idf_component_register(SRCS "main.c"
                    PRIV_REQUIRES wifi-manager LED-manager nvs_flash
                    INCLUDE_DIRS ".")

target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall
        -Werror
)