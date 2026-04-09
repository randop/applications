// Application.hpp - Main application class
#pragma once

#include <vector>
#include "BackgroundLoop.hpp"
#include "HttpClient.hpp"

namespace uvsvc {

class Application {
public:
    void run();

private:
    void runHttpClient();

    std::vector<BackgroundLoop> workers_;
};

} // namespace uvsvc
