file(GLOB SRC_FILES u8g2/csrc/*.c)
idf_component_register(SRCS ${SRC_FILES} INCLUDE_DIRS "u8g2/csrc" REQUIRES driver)
