#include "server.hh"

#include <seastar/core/abort_source.hh>
#include <seastar/core/app-template.hh>
#include <seastar/core/coroutine.hh>
#include <seastar/core/sharded.hh>
#include <seastar/core/signal.hh>
#include <seastar/core/sleep.hh>
#include <seastar/util/log.hh>

#include <chrono>
#include <cstdint>
#include <csignal>

namespace {
seastar::logger mlog("main");
}

// Seastar v25.05 compliant - using coroutines + core idioms
// (do_with, with_gate, abort_source, sharded, maybe_yield, lw_shared_ptr, etc.)
int main(int argc, char** argv) {
    seastar::app_template app;
    namespace bpo = boost::program_options;
    app.add_options()("port", bpo::value<uint16_t>()->default_value(1143),
                      "IMAP listen port (default 1143)");

    return app.run(argc, argv, [&app]() -> seastar::future<int> {
        auto& cfg = app.configuration();
        const uint16_t port = cfg["port"].as<uint16_t>();

        seastar::sharded<imap::imap_server> server;
        seastar::abort_source stop_as;

        try {
            seastar::handle_signal(SIGINT, [&stop_as] { stop_as.request_abort(); });
            seastar::handle_signal(SIGTERM, [&stop_as] { stop_as.request_abort(); });

            co_await server.start();
            co_await server.invoke_on_all([port](imap::imap_server& s) -> seastar::future<> {
                co_await s.start(port);
                co_return;
            });

            mlog.info("barebones IMAP server running on port {}", port);
            mlog.info("demo logins: demo/demo , alice/secret");

            // Block until SIGINT/SIGTERM aborts the sleep (cancellable wait).
            try {
                co_await seastar::sleep_abortable(std::chrono::hours(24 * 365 * 10), stop_as);
            } catch (const seastar::sleep_aborted&) {
                mlog.info("shutdown signal received");
            } catch (const seastar::abort_requested_exception&) {
                mlog.info("shutdown signal received");
            }

            co_await server.stop();
            co_return 0;
        } catch (...) {
            mlog.error("fatal: {}", std::current_exception());
            try {
                co_await server.stop();
            } catch (...) {
            }
            co_return 1;
        }
    });
}
