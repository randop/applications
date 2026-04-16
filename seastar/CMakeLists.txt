cmake_minimum_required(VERSION 3.20)
project(seastar-tcp-echo LANGUAGES CXX)

# Suppress Boost-related CMake warnings
cmake_policy(SET CMP0144 NEW)   # BOOST_ROOT env var casing
cmake_policy(SET CMP0167 NEW)   # FindBoost deprecation
set(Boost_NO_WARN_NEW_VERSIONS ON)  # Boost 1.90 unknown to system FindBoost

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

add_executable(shard-demo sharded_parameter_demo.cc)
target_link_libraries(shard-demo PRIVATE Seastar::seastar)
target_compile_options(shard-demo PRIVATE
    -Wall -Wextra -O3
)

add_executable(hello hello.cc)
target_link_libraries(hello PRIVATE Seastar::seastar)
target_compile_options(hello PRIVATE
    -Wall -Wextra -O3
)

add_executable(timeout-demo timeout.cc)
target_link_libraries(timeout-demo PRIVATE Seastar::seastar)
target_compile_options(timeout-demo PRIVATE
    -Wall -Wextra -O3
)

add_executable(multitask-demo multitask.cc)
target_link_libraries(multitask-demo PRIVATE Seastar::seastar)
target_compile_options(multitask-demo PRIVATE
    -Wall -Wextra -O3
)

add_executable(filewriter filewriter.cc)
target_link_libraries(filewriter PRIVATE Seastar::seastar)
target_compile_options(filewriter PRIVATE
    -Wall -Wextra -O3
)
