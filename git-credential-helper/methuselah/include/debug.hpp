#pragma once
#include <iostream>

#ifdef APP_METHUSELAH_DEBUG
#define DEBUG_LOG(...)                                                         \
  do {                                                                         \
    std::cerr << "[DEBUG] " << __FILE__ << ":" << __LINE__ << " " << __func__  \
              << "() : " << __VA_ARGS__ << std::endl;                          \
  } while (0)

#define DEBUG_IF(cond, ...)                                                    \
  do {                                                                         \
    if (cond) {                                                                \
      DEBUG_LOG(__VA_ARGS__);                                                  \
    }                                                                          \
  } while (0)
#else
#define DEBUG_LOG(...) ((void)0)
#define DEBUG_IF(cond, ...) ((void)0)
#endif
