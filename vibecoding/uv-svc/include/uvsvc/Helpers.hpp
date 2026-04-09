// Helpers.hpp - Utility functions
#pragma once

namespace uvsvc {

// TLS logger callback for tlsuv
void tls_logger(int level, const char* file, unsigned int line, const char* msg);

} // namespace uvsvc
