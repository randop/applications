cmake_minimum_required(VERSION 3.25)

# Find test framework
find_package(Boost 1.89.0 REQUIRED COMPONENTS system unit_test_framework)

# Test executable
add_executable(task_dispatcher_tests
    test_main.cpp
    test_abort_controller.cpp
    test_task_dispatcher.cpp
    test_io_uring_context.cpp
    test_http_client.cpp
    test_git_client.cpp
)

target_include_directories(task_dispatcher_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${Boost_INCLUDE_DIRS}
)

target_link_libraries(task_dispatcher_tests PRIVATE
    Boost::system
    Boost::unit_test_framework
    Threads::Threads
    ${LIBURING_LIBRARIES}
)

# Add test
add_test(NAME task_dispatcher_unit_tests COMMAND task_dispatcher_tests)

# Enable testing
enable_testing()
