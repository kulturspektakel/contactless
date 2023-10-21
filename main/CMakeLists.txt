set(srcs
    "device_id.c"
    "display.c"
    "fetch_config.c"
    "http_auth_headers.c"
    "local_config.c"
    "log_uploader.c"
    "logger.c"
    "keypad.c"
    "main.c"
    "wifi_connect.c"
    "u8g2_esp32_hal.c"
)

idf_component_register(SRCS "${srcs}" INCLUDE_DIRS ".")
