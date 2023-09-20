

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../components/nanopb/extra)
find_package(Nanopb REQUIRED)
include_directories(${NANOPB_INCLUDE_DIRS})
nanopb_generate_cpp(PROTO_SRCS PROTO_HDRS simple.proto)
include_directories(${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Werror -g -O0")
add_executable(simple simple.c ${PROTO_SRCS} ${PROTO_HDRS})

file(GLOB srcs "*.c")
idf_component_register(SRCS "fetch_config.c" "http_event_handler.c" "${srcs}"
    INCLUDE_DIRS ".")
