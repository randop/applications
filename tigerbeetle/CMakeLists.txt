cmake_minimum_required(VERSION 3.20)

# Legacy cmake versions does not support suppression of some warnings
set(FEATURE_MIN_VERSION "4.0.0")

if(CMAKE_VERSION VERSION_GREATER_EQUAL "${FEATURE_MIN_VERSION}")
  # Suppress Boost-related CMake warnings
  cmake_policy(SET CMP0144 NEW)
  cmake_policy(SET CMP0167 NEW)
  set(Boost_NO_WARN_NEW_VERSIONS ON)
endif()

file(STRINGS "VERSION" PROJECT_VERSION_STRING LIMIT_COUNT 1)
string(STRIP "${PROJECT_VERSION_STRING}" PROJECT_VERSION_STRING)

project(mail VERSION ${PROJECT_VERSION_STRING} LANGUAGES CXX)

project(mail LANGUAGES CXX)

# Enable C++23
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  include(CheckIPOSupported)
  check_ipo_supported(RESULT ipo_supported)
  if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
  endif()

  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
      -O3
      -march=x86-64
      -mtune=generic
      -funroll-loops
      -fomit-frame-pointer
      -fno-plt
      -fvisibility=hidden
    )

    add_link_options(
      -Wl,-O1
      -Wl,--as-needed
      -Wl,--strip-all
    )
  endif()

  # Sanitizes and disables assert(); standard
  add_compile_definitions(
    NDEBUG
  )
endif()

find_package(Seastar REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Boost REQUIRED COMPONENTS json)

# -----------
# TigerBeetle
# -----------
set(TB_VERSION "0.17.4")
set(TB_ROOT "$ENV{HOME}/opt/tigerbeetle/${TB_VERSION}"
    CACHE PATH "TigerBeetle installation root")
 
set(TB_INCLUDE_DIR "${TB_ROOT}/src/clients/c")
set(TB_LIB_DIR     "${TB_ROOT}/src/clients/c/lib/x86_64-linux-gnu.2.27")

add_executable(app
  src/main.cpp
  src/client.cpp
)

add_library(tb_client STATIC IMPORTED)
set_target_properties(tb_client PROPERTIES
    IMPORTED_LOCATION             "${TB_LIB_DIR}/libtb_client.a"
    INTERFACE_INCLUDE_DIRECTORIES "${TB_INCLUDE_DIR}"
)

target_link_libraries(app PRIVATE Seastar::seastar)
target_link_libraries(app
  PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto
)
target_link_libraries(app PRIVATE Boost::json)

target_include_directories(app PRIVATE
    include
    "${TB_INCLUDE_DIR}"
)

target_compile_options(app PRIVATE
    -Wall -Wextra
)

target_link_libraries(app PRIVATE
    tb_client
    ${CMAKE_DL_LIBS}
    m
    rt
)

target_compile_definitions(app PRIVATE
  PROJECT_VERSION="${PROJECT_VERSION_STRING}"
)
