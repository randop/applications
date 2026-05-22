#include <seastar/core/app-template.hh>
#include <seastar/core/sharded.hh>
#include <seastar/core/reactor.hh>
#include <seastar/net/api.hh>
#include <seastar/net/tls.hh>
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
#include <string_view>

using namespace seastar;

logger app_log("echo");

std::string generate_random_name() {
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, sizeof(alphabet) - 2);
    std::string s = "echo_";
    for (int i = 0; i < 10; ++i) s += alphabet[dis(gen)];
    return s + ".log";
}

future<> handle_connection(connected_socket s, socket_address addr, shared_ptr<tls::server_credentials> certs) {
    auto filename = generate_random_name();
    
    std::optional<output_stream<char>> out;
    std::optional<input_stream<char>> in;
    std::optional<output_stream<char>> fout;
    
    auto close_all = [&]() -> future<> {
        if (fout) {
            co_await fout->close();
            fout = std::nullopt;
        }
        if (out) {
            co_await out->close();
            out = std::nullopt;
        }
        in = std::nullopt;
    };

    try {
        file f = co_await open_file_dma(filename, open_flags::rw | open_flags::create | open_flags::truncate);
        out = s.output();
        in = s.input();
        fout = co_await make_file_output_stream(std::move(f));

        while (true) {
            temporary_buffer<char> buf = co_await in->read();
            if (buf.empty()) {
                break;
            }

            std::string_view data(buf.get(), buf.size());
            size_t pos = data.find("TLS;");
            
            if (pos != std::string_view::npos) {
                if (pos > 0) {
                    co_await fout->write(data.data(), pos);
                    co_await out->write(data.data(), pos);
                    co_await out->flush();
                }
                
                co_await out->flush();
                co_await out->close();
                out = std::nullopt;
                in = std::nullopt;
                
                try {
                    s = co_await tls::wrap_server(certs, std::move(s));
                    app_log.info("Connection upgraded to TLS for {}", addr);
                } catch (const std::exception& e) {
                    app_log.error("TLS upgrade failed for {}: {}", addr, e.what());
                    throw;
                }
                
                in = s.input();
                out = s.output();
                co_await out->write("Hello World\n");
                co_await out->flush();
                
                if (pos + 4 < data.size()) {
                    auto remaining = data.substr(pos + 4);
                    co_await fout->write(remaining.data(), remaining.size());
                    co_await out->write(remaining.data(), remaining.size());
                    co_await out->flush();
                }
                continue;
            }

            co_await fout->write(buf.get(), buf.size());
            co_await out->write(std::move(buf));
            co_await out->flush();
        }

        co_await close_all();
        
        std::string target = "/tmp/" + filename;
        bool move_failed = false;
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
        app_log.error("Connection error ({}): {}", addr, e.what());
    }
    
    co_await close_all().handle_exception([](auto){});
}

future<> service_loop() {
    auto certs = make_shared<tls::server_credentials>();
    co_await certs->set_x509_key_file("cert.pem", "key.pem", tls::x509_crt_format::PEM);

    listen_options lo;
    lo.reuse_address = true;
    server_socket listener = listen(make_ipv4_address({1234}), lo);
    app_log.info("Echo server listening on port 1234...");
    
    while (true) {
        try {
            accept_result res = co_await listener.accept();
            (void)handle_connection(std::move(res.connection), std::move(res.remote_address), certs);
        } catch (const std::exception& e) {
            app_log.error("Accept error: {}", e.what());
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
