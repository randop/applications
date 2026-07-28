Define `SPDLOG_USE_STD_FORMAT` before including spdlog headers — this switches spdlog's backend from bundled fmt to `std::format`.

**main.cpp**
```cpp
#define SPDLOG_USE_STD_FORMAT
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

int main() {
    auto logger = spdlog::stdout_color_mt("console");
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

    int id = 42;
    double value = 3.14159;

    spdlog::info("Starting service");
    spdlog::debug("Loaded config id={} value={:.2f}", id, value);
    spdlog::warn("Queue depth {} exceeds threshold", 128);
    spdlog::error("Failed to connect to {}:{}", "127.0.0.1", 5432);

    return 0;
}
```

**CMakeLists.txt**
```cmake
cmake_minimum_required(VERSION 3.28)
project(spdlog_std_format CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(spdlog REQUIRED)

add_executable(spdlog_std_format main.cpp)
target_link_libraries(spdlog_std_format PRIVATE spdlog::spdlog)
target_compile_definitions(spdlog_std_format PRIVATE SPDLOG_USE_STD_FORMAT)
```

Notes:

- Requires spdlog ≥ 1.11 and a libstdc++/libc++ with `<format>` support (GCC 13+, Clang 17+).
- With `std::format` as backend, format strings are compile-time checked — malformed specifiers become compile errors, not runtime exceptions like with fmt's runtime-string mode.
- `SPDLOG_USE_STD_FORMAT` must be defined identically for every TU that includes spdlog headers, or you get ODR violations. Safest to set it via `target_compile_definitions` globally, not per-file `#define`.
- If spdlog is vendored (FetchContent/submodule) instead of found via `find_package`, add the same `target_compile_definitions` to that target too.

