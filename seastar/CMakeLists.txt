cmake_minimum_required(VERSION 3.20)
project(seastar-tcp-echo LANGUAGES CXX)

# Enable C++23
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Seastar REQUIRED)

add_executable(tcp-echo main.cc)

target_link_libraries(tcp-echo PRIVATE Seastar::seastar)

target_compile_options(tcp-echo PRIVATE
    -Wall -Wextra -O3
)
