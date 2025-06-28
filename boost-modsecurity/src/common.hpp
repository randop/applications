#ifndef APP_COMMON_HPP
#define APP_COMMON_HPP

#include <string>
#include <memory>

namespace App {
// Use string_view for memory efficiency
using StringView = std::string_view;

// Forward declarations to reduce compile-time dependencies
class ModSecurityFilter;
class HttpConnection;
}  // namespace App

#endif  // APP_COMMON_HPP