cmake_minimum_required(VERSION 3.20)
project(tls_uv_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

add_executable(https_server main.cpp)
target_link_libraries(https_server PRIVATE uv OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
