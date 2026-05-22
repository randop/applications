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
#include <vector>
#include <memory>

using namespace seastar;

logger app_log("smtp");

std::string generate_random_name() {
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, sizeof(alphabet) - 2);
    std::string s = "smtp_";
    for (int i = 0; i < 10; ++i) s += alphabet[dis(gen)];
    return s + ".log";
}

struct smtp_session {
    connected_socket s;
    std::unique_ptr<output_stream<char>> out;
    input_stream<char> in;
    std::unique_ptr<output_stream<char>> fout;
    bool is_tls = false;

    smtp_session(connected_socket s, output_stream<char> fout_obj) 
        : s(std::move(s))
        , out(std::make_unique<output_stream<char>>(this->s.output()))
        , in(this->s.input())
        , fout(std::make_unique<output_stream<char>>(std::move(fout_obj))) {}

    future<> send(std::string_view msg) {
        if (fout) co_await fout->write(msg.data(), msg.size());
        if (out) {
            co_await out->write(msg.data(), msg.size());
            co_await out->flush();
        }
    }

    future<> upgrade_tls(shared_ptr<tls::server_credentials> certs) {
        if (out) {
            co_await out->flush();
            (void)out.release(); 
        }
        in = {}; 
        
        s = co_await tls::wrap_server(certs, std::move(s));
        out = std::make_unique<output_stream<char>>(s.output());
        in = s.input();
        is_tls = true;
    }

    future<> close() {
        if (out) {
            co_await out->close();
            out = nullptr;
        }
        if (fout) {
            co_await fout->close();
            fout = nullptr;
        }
    }
};

future<> handle_connection(connected_socket s, socket_address addr, shared_ptr<tls::server_credentials> certs) {
    auto filename = generate_random_name();
    app_log.info("Session {} created log file {}", addr, filename);
    std::unique_ptr<smtp_session> sess;
    try {
        file f = co_await open_file_dma(filename, open_flags::rw | open_flags::create | open_flags::truncate);
        sess = std::make_unique<smtp_session>(std::move(s), co_await make_file_output_stream(std::move(f)));

        co_await sess->send("220 mail.example.com ESMTP ready\r\n");

        std::string buffer;
        bool in_data = false;

        while (true) {
            temporary_buffer<char> buf = co_await sess->in.read();
            if (buf.empty()) break;

            if (sess->fout) co_await sess->fout->write(buf.get(), buf.size());
            buffer.append(buf.get(), buf.size());

            while (true) {
                if (in_data) {
                    size_t pos = buffer.find("\r\n.\r\n");
                    if (pos != std::string::npos) {
                        in_data = false;
                        buffer.erase(0, pos + 5);
                        co_await sess->send("250 Message queued\r\n");
                        continue;
                    }
                    break;
                }

                size_t pos = buffer.find("\r\n");
                if (pos == std::string::npos) break;

                std::string line = buffer.substr(0, pos);
                buffer.erase(0, pos + 2);
                std::string_view cmd = line;

                if (cmd.starts_with("EHLO")) {
                    std::string resp = "250-mail.example.com\r\n";
                    if (!sess->is_tls) resp += "250-STARTTLS\r\n";
                    resp += "250-AUTH PLAIN LOGIN\r\n250 SIZE 52428800\r\n";
                    co_await sess->send(resp);
                } else if (cmd.starts_with("STARTTLS")) {
                    co_await sess->send("220 Ready to start TLS\r\n");
                    co_await sess->upgrade_tls(certs);
                    app_log.info("Session {} upgraded to TLS", addr);
                    buffer.clear();
                    break; 
                } else if (cmd.starts_with("AUTH")) {
                    co_await sess->send("235 Authentication successful\r\n");
                } else if (cmd.starts_with("MAIL FROM:")) {
                    co_await sess->send("250 OK\r\n");
                } else if (cmd.starts_with("RCPT TO:")) {
                    co_await sess->send("250 OK\r\n");
                } else if (cmd.starts_with("DATA")) {
                    in_data = true;
                    co_await sess->send("354 Start input, end with CRLF.CRLF\r\n");
                } else if (cmd.starts_with("QUIT")) {
                    co_await sess->send("221 Bye\r\n");
                    goto sess_done;
                } else if (!cmd.empty()) {
                    co_await sess->send("500 Unrecognized command\r\n");
                }
            }
        }
sess_done:
        co_await sess->close();
        
        std::string target = "/tmp/" + filename;
        bool move_failed = false;
        try {
            co_await rename_file(filename, target);
            app_log.info("Session {} log file moved to {}", addr, target);
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
            app_log.info("Session {} log file copied to {}", addr, target);
        }
    } catch (const std::exception& e) {
        app_log.error("SMTP Error ({}): {}", addr, e.what());
    }
    
    if (sess) {
        co_await sess->close().handle_exception([](auto){});
    }
}

future<> service_loop() {
    auto certs = make_shared<tls::server_credentials>();
    co_await certs->set_x509_key_file("cert.pem", "key.pem", tls::x509_crt_format::PEM);

    listen_options lo;
    lo.reuse_address = true;
    server_socket listener = listen(make_ipv4_address({1234}), lo);
    app_log.info("SMTP server listening on port 1234...");
    
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
