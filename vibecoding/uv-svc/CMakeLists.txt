cmake_minimum_required(VERSION 3.15)
project(uv-svc VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(libuv REQUIRED IMPORTED_TARGET libuv>=1.0.0)

add_subdirectory(vendor/llhttp-release-v9.3.1)

file(WRITE "${CMAKE_BINARY_DIR}/llhttp-config.cmake"
"set(llhttp_FOUND TRUE)
set(llhttp_VERSION \"9.3.1\")
add_library(llhttp::llhttp_shared SHARED IMPORTED)
set_target_properties(llhttp::llhttp_shared PROPERTIES
IMPORTED_LOCATION \"${CMAKE_BINARY_DIR}/vendor/llhttp-release-v9.3.1/libllhttp.so\"
INTERFACE_INCLUDE_DIRECTORIES \"${CMAKE_CURRENT_SOURCE_DIR}/vendor/llhttp-release-v9.3.1/include\"
)
set(llhttp_LIBRARIES llhttp::llhttp_shared)
set(llhttp_INCLUDE_DIRS \"${CMAKE_CURRENT_SOURCE_DIR}/vendor/llhttp-release-v9.3.1/include\")
")

set(CMAKE_PREFIX_PATH "${CMAKE_BINARY_DIR}" ${CMAKE_PREFIX_PATH})
set(TLSUV_HTTP ON CACHE BOOL "Enable HTTP support in tlsuv" FORCE)
add_subdirectory(vendor/tlsuv-0.41.1)

add_executable(uv-svc src/main.cpp)

target_link_libraries(uv-svc PRIVATE
    tlsuv
    PkgConfig::libuv
)

target_include_directories(uv-svc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)
