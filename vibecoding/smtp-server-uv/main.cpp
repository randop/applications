#include <uv.h>
#include <iostream>
#include "smtp_session.h"

static uv_loop_t* loop = nullptr;

static void on_new_connection(uv_stream_t* server, int status) {
    if (status < 0) {
        std::cerr << "New connection error: " << uv_strerror(status) << std::endl;
        return;
    }

    auto* client = new uv_tcp_t;
    uv_tcp_init(loop, client);

    if (uv_accept(server, (uv_stream_t*)client) == 0) {
        auto* session = new SmtpSession(client);
        session->start();
    } else {
        uv_close((uv_handle_t*)client, nullptr);
        delete client;
    }
}

int main() {
    int port = 25999;
    loop = uv_default_loop();

    uv_tcp_t server;
    uv_tcp_init(loop, &server);

    struct sockaddr_in addr;
    uv_ip4_addr("0.0.0.0", port, &addr);

    uv_tcp_bind(&server, (const struct sockaddr*)&addr, 0);
    int r = uv_listen((uv_stream_t*)&server, SOMAXCONN, on_new_connection);
    if (r) {
        std::cerr << "Listen error: " << uv_strerror(r) << std::endl;
        return 1;
    }

    std::cout << "smtp server listening on port " << port << std::endl;
    uv_run(loop, UV_RUN_DEFAULT);

    uv_loop_close(loop);
    return 0;
}
