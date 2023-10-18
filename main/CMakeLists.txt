set(srcs
    "device_id.c"
    "fetch_config.c"
    "http_auth_headers.c"
    "local_config.c"
    "log_uploader.c"
    "logger.c"
    "main.c"
    "wifi_connect.c"
)

idf_component_register(SRCS "${srcs}" INCLUDE_DIRS ".")
