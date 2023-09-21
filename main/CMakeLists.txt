file(GLOB SRCS "*.c")
idf_component_register(SRCS "fetch_config.c" "http_event_handler.c" "${srcs}"
    INCLUDE_DIRS ".")
