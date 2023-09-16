set(srcs "main.c" "wifi_connect.c")

idf_component_register(SRCS "${srcs}"
    INCLUDE_DIRS ".")
