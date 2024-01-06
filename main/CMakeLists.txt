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
    "power_management.c"
    "rfid.c"
    "state_machine.c"
    "wifi_connect.c"
    "u8g2_esp32_hal.c"
    "time_sync.c"
)

idf_component_register(SRCS "${srcs}" INCLUDE_DIRS ".")