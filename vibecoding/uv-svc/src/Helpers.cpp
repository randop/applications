// Helpers.cpp - Utility functions implementation
#include "uvsvc/Helpers.hpp"
#include "uvsvc/SafeOutput.hpp"

#include <ctime>
#include <mutex>
#include <iostream>

namespace uvsvc {

void tls_logger(int level, const char* file, unsigned int line, const char* msg) {
    struct timespec spec;
    clock_gettime(CLOCK_REALTIME, &spec);
    std::lock_guard<std::mutex> lock(SafeOutput::getMutex());
    std::cerr << "[" << spec.tv_sec << "." << spec.tv_nsec/1000000 << "] "
              << file << ":" << line << " " << msg << std::endl;
}

} // namespace uvsvc
