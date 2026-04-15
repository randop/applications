#include <seastar/core/app-template.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/future-util.hh>
#include <seastar/net/api.hh>
#include <seastar/util/log.hh>
#include <seastar/core/loop.hh>
#include <seastar/core/do_with.hh>
#include <seastar/core/gate.hh>

namespace bpo = boost::program_options;
static seastar::logger applog("tcp-echo");

seastar::future<> handle_connection(seastar::connected_socket sock, seastar::socket_address addr) {
    applog.info("Accepted connection from {}", addr);

    auto in  = sock.input();
    auto out = sock.output();

    return seastar::do_with(
        std::move(in), std::move(out),
        [](auto& in, auto& out) -> seastar::future<> {
            return seastar::keep_doing([&in, &out]() -> seastar::future<> {
                // Read up to the stream's internal buffer size (usually efficient)
                return in.read().then([&out](seastar::temporary_buffer<char> buf) {
                    if (buf.empty()) {
                        // Client closed connection
                        return seastar::make_ready_future<>();
                    }

                    // Echo back exactly what we received
                    return out.write(std::move(buf)).then([&out] {
                        return out.flush();
                    });
                });
            }).finally([&in, &out] {
                return out.close().then([&in] {
                    return in.close();
                });
            });
        });
}

seastar::future<> service_loop(uint16_t port, seastar::gate& gate) {
    seastar::listen_options lo;
    lo.reuse_address = true;

    return seastar::do_with(
        seastar::listen(seastar::make_ipv4_address({port}), lo),
        [port, &gate](auto& listener) -> seastar::future<> {
            applog.info("TCP Echo server listening on port {}", port);

            return seastar::repeat([&listener, &gate]() -> seastar::future<seastar::stop_iteration> {
                if (seastar::engine().stopped()) {
                    return seastar::make_ready_future<seastar::stop_iteration>(seastar::stop_iteration::yes);
                }

                return listener.accept().then([&gate](seastar::accept_result res) {
                    return seastar::with_gate(gate, [conn = std::move(res.connection),
                                                     addr = res.remote_address]() mutable {
                        return handle_connection(std::move(conn), addr);
                    }).then([] {
                        return seastar::stop_iteration::no;
                    });
                });
            });
        });
}

int main(int argc, char** argv) {
    seastar::app_template app;

    app.add_options()
                    ("port,p", bpo::value<uint16_t>()->default_value(10000), "Server port");

    return app.run(argc, argv, [&app] {
        auto& cfg = app.configuration();
        uint16_t port = cfg["port"].as<uint16_t>();

        seastar::gate gate;

        return seastar::smp::invoke_on_all([port, &gate] {
            return service_loop(port, gate);
        }).finally([&gate] {
            // Wait for all active connections to finish
            applog.info("Shutting down... waiting for active connections to close");
            return gate.close();
        }).handle_exception([](std::exception_ptr ep) {
            applog.error("Fatal error: {}", ep);
            return seastar::make_exception_future<>(ep);
        });
    });
}
