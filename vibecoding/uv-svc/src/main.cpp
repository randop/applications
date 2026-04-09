// main.cpp - Application entry point
#include "uvsvc/Application.hpp"
#include "uvsvc/Helpers.hpp"

#include <tlsuv/tlsuv.h>

int main() {
    tlsuv_set_debug(1, uvsvc::tls_logger);

    uvsvc::Application app;
    app.run();

    return 0;
}
