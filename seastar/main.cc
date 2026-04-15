#include <seastar/core/app-template.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/future-util.hh>
#include <seastar/net/api.hh>
#include <seastar/util/log.hh>
#include <seastar/core/loop.hh>
#include <seastar/core/do_with.hh>
#include <seastar/core/thread.hh>
#include <seastar/core/sleep.hh>
#include <seastar/core/sharded.hh>
#include <seastar/core/gate.hh>
#include <seastar/net/api.hh>
#include <seastar/net/dns.hh>
#include <seastar/util/closeable.hh>
#include <iostream>

#include "stop_signal.hh"

static seastar::logger applog("tcp-echo");

using namespace seastar;
namespace bpo = boost::program_options;


struct streams {
    connected_socket s;
    input_stream<char> in;
    output_stream<char> out;

    streams(connected_socket cs) : s(std::move(cs)), in(s.input()), out(s.output())
    {}
};

class echoserver {
    server_socket _socket;
    seastar::gate _gate;
    bool _stopped = false;
    bool _verbose = false;

public:
    echoserver(bool verbose = false) : _verbose(verbose) {}

    future<> listen(socket_address addr) {
        listen_options opts;
        opts.reuse_address = true;

        _socket = seastar::listen(seastar::make_ipv4_address(addr), opts);

        // Background accept loop
        (void)repeat([this] {
            if (_stopped) {
                return make_ready_future<stop_iteration>(stop_iteration::yes);
            }

            return with_gate(_gate, [this] {
                return _socket.accept().then([this](accept_result ar) {
                    connected_socket s = std::move(ar.connection);
                    socket_address a = std::move(ar.remote_address);

                    if (_verbose) {
                        std::cout << "Got connection from " << a << std::endl;
                    }

                    auto strms = make_lw_shared<streams>(std::move(s));

                    return repeat([strms, this]() mutable {
                        return strms->in.read().then([this, strms](temporary_buffer<char> buf) {
                            if (buf.empty()) {
                                if (_verbose) {
                                    std::cout << "EOM" << std::endl;
                                }
                                return make_ready_future<stop_iteration>(stop_iteration::yes);
                            }

                            sstring tmp(buf.begin(), buf.end());
                            if (_verbose) {
                                std::cout << "Read " << tmp.size() << "B" << std::endl;
                            }

                            return strms->out.write(std::move(tmp)).then([strms]() {
                                return strms->out.flush();
                            }).then([] {
                                return make_ready_future<stop_iteration>(stop_iteration::no);
                            });
                        });
                    }).then([strms] {
                        return strms->out.close();
                    }).handle_exception([](std::exception_ptr) {
                        // Ignore per-connection errors (removed unused 'ep')
                    }).finally([this, strms] {
                        if (_verbose) {
                            std::cout << "Ending session" << std::endl;
                        }
                        return strms->in.close();
                    });
                }).handle_exception([this](auto ep) {
                    if (!_stopped) {
                        std::cerr << "Accept error: " << ep << std::endl;
                    }
                }).then([this] {
                    return make_ready_future<stop_iteration>(_stopped ? stop_iteration::yes : stop_iteration::no);
                });
            });
        });

        return make_ready_future<>();
    }

    future<> stop() {
        _stopped = true;
        _socket.abort_accept();
        return _gate.close();
    }
};

int main(int ac, char** av) {
    app_template app;
    app.add_options()
        ("port", bpo::value<uint16_t>()->default_value(10000), "Server port")
        ("address", bpo::value<std::string>()->default_value("0.0.0.0"), "Server address")
        ("verbose,v", bpo::value<bool>()->default_value(true)->implicit_value(true), "Verbose output")
        ;

    return app.run(ac, av, [&app] {
        return async([&app] {
            seastar_apps_lib::stop_signal stop_signal;
            auto&& config = app.configuration();

            uint16_t port = config["port"].as<uint16_t>();
            std::string addr_str = config["address"].as<std::string>();
            bool verbose = config["verbose"].as<bool>();

             applog.info("Starting plain TCP echo server...");

            net::inet_address a = net::dns::resolve_name(addr_str).get();
            ipv4_addr ia(a, port);
            socket_address sa(ia);

            seastar::sharded<echoserver> server;
            server.start(verbose).get();
            auto stop_server = deferred_stop(server);

              try {
                server.invoke_on_all(&echoserver::listen, sa).get();
            } catch (...) {
                std::cerr << "Failed to start server: " << std::current_exception() << std::endl;
                return 1;
            }

            std::cout << "Plain TCP echo server running at " << addr_str << ":" << port << std::endl;
            std::cout << "Press Ctrl+C to stop..." << std::endl;

            stop_signal.wait().get();
            return 0;

        });
    });
}

