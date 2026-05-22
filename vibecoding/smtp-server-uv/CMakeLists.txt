cmake_minimum_required(VERSION 3.20)
project(smtp_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Use pkg-config to locate libuv
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUV REQUIRED IMPORTED_TARGET libuv)

add_executable(smtp_server
    main.cpp
    smtp_session.cpp
    smtp_parser.cpp
)

# Link using the modern imported target from pkg-config
target_link_libraries(smtp_server PRIVATE PkgConfig::LIBUV)

# Optional: Print libuv info during configure (helpful for debugging)
message(STATUS "libuv found via pkg-config:")
message(STATUS "  Version: ${LIBUV_VERSION}")
message(STATUS "  Include dirs: ${LIBUV_INCLUDE_DIRS}")
message(STATUS "  Libraries: ${LIBUV_LIBRARIES}")
