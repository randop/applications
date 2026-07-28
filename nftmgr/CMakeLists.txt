cmake_minimum_required(VERSION 3.20)
project(nftmgr CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(NFTABLES REQUIRED IMPORTED_TARGET libnftables)

add_library(nftmgr_core
    src/nft_context.cpp
    src/blocklist_manager.cpp
)
target_include_directories(nftmgr_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(nftmgr_core PUBLIC PkgConfig::NFTABLES)
target_compile_options(nftmgr_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(nftmgr src/main.cpp)
target_link_libraries(nftmgr PRIVATE nftmgr_core)
target_compile_options(nftmgr PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS nftmgr RUNTIME DESTINATION bin)
