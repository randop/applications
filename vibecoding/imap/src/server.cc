#include "server.hh"

#include "connection.hh"

#include <seastar/core/coroutine.hh>
#include <seastar/core/reactor.hh>
#include <seastar/net/api.hh>
#include <seastar/util/log.hh>

#include <system_error>

namespace imap {

static seastar::logger ilog("imap");

seastar::future<> imap_server::start(uint16_t port) {
    _port = port;
    seastar::listen_options lo;
    lo.reuse_address = true;
    // Distribute accepts across shards when supported.
    lo.lba = seastar::server_socket::load_balancing_algorithm::connection_distribution;

    _listener = seastar::listen(seastar::socket_address(seastar::ipv4_addr{_port}), lo);
    _started = true;
    ilog.info("IMAP listening on port {} (shard {})", _port, seastar::this_shard_id());

    // Fire-and-forget accept loop under the gate.
    (void)seastar::with_gate(_gate, [this]() -> seastar::future<> {
        co_await accept_loop();
        co_return;
    });
    co_return;
}

seastar::future<> imap_server::accept_loop() {
    while (!_as.abort_requested()) {
        try {
            auto accepted = co_await _listener.accept();
            auto sock = std::move(accepted.connection);
            auto remote = std::move(accepted.remote_address);

            // Detach connection handler; track with gate for shutdown.
            (void)seastar::with_gate(_gate, [this, sock = std::move(sock),
                                             remote = std::move(remote)]() mutable
                                         -> seastar::future<> {
                try {
                    co_await handle_connection(std::move(sock), std::move(remote));
                } catch (const seastar::gate_closed_exception&) {
                    // Shutting down.
                } catch (...) {
                    ilog.debug("connection ended with exception: {}", std::current_exception());
                }
                co_return;
            });
        } catch (const std::system_error& e) {
            if (_as.abort_requested()) {
                break;
            }
            // accept aborted / socket closed during stop
            if (e.code() == std::errc::connection_aborted
                || e.code() == std::errc::bad_file_descriptor
                || e.code() == std::errc::invalid_argument) {
                break;
            }
            ilog.warn("accept failed: {}", e.what());
        } catch (const seastar::gate_closed_exception&) {
            break;
        } catch (...) {
            if (_as.abort_requested()) {
                break;
            }
            ilog.warn("accept failed: {}", std::current_exception());
        }
    }
    co_return;
}

seastar::future<> imap_server::handle_connection(seastar::connected_socket sock,
                                                 seastar::socket_address remote) {
    ilog.info("connection from {}", remote);
    imap_connection conn(std::move(sock), std::move(remote), _auth, _as);
    co_await conn.run();
    co_return;
}

seastar::future<> imap_server::stop() {
    if (!_started) {
        co_return;
    }
    _as.request_abort();
    _listener.abort_accept();
    co_await _gate.close();
    co_return;
}

} // namespace imap
