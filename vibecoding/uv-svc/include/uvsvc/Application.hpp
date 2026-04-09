// Application.hpp - Main application class
#pragma once

#include <vector>
#include <uv.h>
#include "BackgroundLoop.hpp"
#include "HttpClient.hpp"

namespace uvsvc {

class Application {
public:
    void run();

private:
    void runHttpClient();

    std::vector<BackgroundLoop> workers_;
    uv_mutex_t ip_request_mutex_;
};

} // namespace uvsvc
