file(GLOB SRCS "*.c")
idf_component_register(SRCS "main.c" "fetch_config.c" "local_config.c" "wifi_connect.c" "${srcs}"
    INCLUDE_DIRS ".")
