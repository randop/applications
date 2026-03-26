cmake_minimum_required(VERSION 3.25)
project(TaskDispatcher VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find packages
find_package(Boost 1.89.0 REQUIRED COMPONENTS system url)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBURING REQUIRED liburing>=2.0)

# Main executable
add_executable(task_dispatcher
    src/main.cpp
    src/dispatcher/task_dispatcher.cpp
    src/dispatcher/io_uring_context.cpp
    src/dispatcher/abort_controller.cpp
    src/net/http_client.cpp
    src/net/git_client.cpp
    src/utils/timeout.cpp
)

target_include_directories(task_dispatcher PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${LIBURING_INCLUDE_DIRS}
)

target_link_libraries(task_dispatcher PRIVATE
    Boost::system
    Boost::url
    Threads::Threads
    ${LIBURING_LIBRARIES}
)

target_compile_options(task_dispatcher PRIVATE
    ${LIBURING_CFLAGS_OTHER}
    -Wall -Wextra -Wpedantic
)

# Tests
enable_testing()
add_subdirectory(tests)
