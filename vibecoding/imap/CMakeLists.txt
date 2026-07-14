cmake_minimum_required(VERSION 3.20)
project(imap-server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Seastar REQUIRED)

add_executable(imap-server
  src/main.cc
  src/server.cc
  src/connection.cc
  src/protocol/parser.cc
  src/protocol/response.cc
  src/mailbox/store.cc
  src/auth/authenticator.cc
)

target_include_directories(imap-server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(imap-server PRIVATE Seastar::seastar)

# Seastar v25.05: prefer C++23 coroutines; ensure standard is enforced.
target_compile_options(imap-server PRIVATE -std=c++23)
