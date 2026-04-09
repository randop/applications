// SafeOutput.cpp - Thread-safe output implementation
#include "uvsvc/SafeOutput.hpp"

namespace uvsvc {

std::mutex SafeOutput::mutex_;

void SafeOutput::print(const std::string& msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << msg << std::endl;
}

std::string SafeOutput::threadId() {
    std::ostringstream oss;
    oss << std::this_thread::get_id();
    return oss.str();
}

std::mutex& SafeOutput::getMutex() {
    return mutex_;
}

} // namespace uvsvc
