#include <seastar/core/app-template.hh>
#include <seastar/core/sharded.hh>
#include <seastar/core/reactor.hh>
#include <seastar/net/api.hh>
#include <seastar/util/log.hh>
#include <seastar/core/fstream.hh>
#include <seastar/core/file.hh>
#include <seastar/core/seastar.hh>
#include <seastar/core/do_with.hh>
#include <seastar/core/loop.hh>
#include <iostream>
#include <random>
#include <string>

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

struct connection_state {
    connected_socket s;
    output_stream<char> out;
    input_stream<char> in;
    output_stream<char> fout;
    connection_state(connected_socket s, output_stream<char> out, input_stream<char> in, output_stream<char> fout) 
        : s(std::move(s)), out(std::move(out)), in(std::move(in)), fout(std::move(fout)) {}
};

future<> handle_connection(connected_socket s, socket_address addr) {
    auto filename = generate_random_name();
    return open_file_dma(filename, open_flags::rw | open_flags::create | open_flags::truncate).then([s = std::move(s)] (file f) mutable {
        auto out = s.output();
        auto in = s.input();
        return make_file_output_stream(std::move(f)).then([s = std::move(s), out = std::move(out), in = std::move(in)] (output_stream<char> fout) mutable {
            return do_with(connection_state(std::move(s), std::move(out), std::move(in), std::move(fout)), [] (connection_state& state) {
                return repeat([&state] {
                    return state.in.read().then([&state] (temporary_buffer<char> buf) {
                        if (buf.empty()) {
                            return make_ready_future<stop_iteration>(stop_iteration::yes);
                        }
                        return state.fout.write(buf.get(), buf.size()).then([&state, buf = std::move(buf)] () mutable {
                            return state.out.write(std::move(buf));
                        }).then([&state] {
                            return state.out.flush();
                        }).then([] {
                            return stop_iteration::no;
                        });
                    });
                }).finally([&state] {
                    return state.fout.close().finally([&state] {
                        return state.out.close();
                    });
                });
            });
        });
    }).handle_exception([] (std::exception_ptr ep) {
        try {
            std::rethrow_exception(ep);
        } catch (const std::exception& e) {
            std::cerr << "Connection error: " << e.what() << "\n";
        }
    });
}

future<> service_loop() {
    listen_options lo;
    lo.reuse_address = true;
    return do_with(listen(make_ipv4_address({1234}), lo), [] (auto& listener) {
        return keep_doing([&listener] {
            return listener.accept().then([] (accept_result res) {
                (void)handle_connection(std::move(res.connection), std::move(res.remote_address));
            });
        });
    });
}

int main(int argc, char** argv) {
    app_template app;
    return app.run(argc, argv, [] {
        return service_loop();
    });
}
