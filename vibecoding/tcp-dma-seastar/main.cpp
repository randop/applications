#include <seastar/core/app-template.hh>
#include <seastar/core/sharded.hh>
#include <seastar/core/reactor.hh>
#include <seastar/net/api.hh>
#include <seastar/util/log.hh>
#include <seastar/core/fstream.hh>
#include <seastar/core/file.hh>
#include <seastar/core/seastar.hh>
#include <seastar/core/coroutine.hh>
#include <seastar/core/loop.hh>
#include <iostream>
#include <random>
#include <string>
#include <filesystem>

using namespace seastar;

std::string generate_random_name() {
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, sizeof(alphabet) - 2);
    std::string s = "echo_";
    for (int i = 0; i < 10; ++i) s += alphabet[dis(gen)];
    return s + ".log";
}

future<> handle_connection(connected_socket s, socket_address addr) {
    auto filename = generate_random_name();
    bool move_failed = false;
    try {
        file f = co_await open_file_dma(filename, open_flags::rw | open_flags::create | open_flags::truncate);
        auto out = s.output();
        auto in = s.input();
        auto fout = co_await make_file_output_stream(std::move(f));

        while (true) {
            temporary_buffer<char> buf = co_await in.read();
            if (buf.empty()) {
                break;
            }
            co_await fout.write(buf.get(), buf.size());
            co_await out.write(std::move(buf));
            co_await out.flush();
        }

        co_await fout.close();
        co_await out.close();
        
        std::string target = "/tmp/" + filename;
        try {
            co_await rename_file(filename, target);
        } catch (...) {
            move_failed = true;
        }
        
        if (move_failed) {
            file src = co_await open_file_dma(filename, open_flags::ro);
            file dst = co_await open_file_dma(target, open_flags::wo | open_flags::create | open_flags::truncate);
            auto is = make_file_input_stream(std::move(src));
            auto os = co_await make_file_output_stream(std::move(dst));
            co_await copy(is, os);
            co_await os.close();
            co_await remove_file(filename);
        }
    } catch (const std::exception& e) {
        std::cerr << "Connection error (" << addr << "): " << e.what() << "\n";
    }
}

future<> service_loop() {
    listen_options lo;
    lo.reuse_address = true;
    server_socket listener = listen(make_ipv4_address({1234}), lo);
    std::cout << "Echo server listening on port 1234...\n";
    
    while (true) {
        try {
            accept_result res = co_await listener.accept();
            (void)handle_connection(std::move(res.connection), std::move(res.remote_address));
        } catch (const std::exception& e) {
            std::cerr << "Accept error: " << e.what() << "\n";
        }
    }
}

int main(int argc, char** argv) {
    app_template app;
    return app.run(argc, argv, [] () -> future<int> {
        co_await service_loop();
        co_return 0;
    });
}
