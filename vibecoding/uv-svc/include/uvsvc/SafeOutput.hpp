// SafeOutput.hpp - Thread-safe console output
#pragma once

#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace uvsvc {

class SafeOutput {
public:
    static void print(const std::string& msg);
    static std::string threadId();
    static std::mutex& getMutex();

private:
    static std::mutex mutex_;
};

#define LOG_INFO(msg) uvsvc::SafeOutput::print(std::string("[INFO] ") + (msg))
#define LOG_ERROR(msg) uvsvc::SafeOutput::print(std::string("[ERROR] ") + (msg))
#define LOG_THREAD(msg) uvsvc::SafeOutput::print("[" + uvsvc::SafeOutput::threadId() + "] " + (msg))

} // namespace uvsvc
