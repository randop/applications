#pragma once

#include "auth/authenticator.hh"

#include <seastar/core/abort_source.hh>
#include <seastar/core/future.hh>
#include <seastar/core/gate.hh>
#include <seastar/net/api.hh>

#include <cstdint>

namespace imap {

// Per-shard IMAP TCP service: listen, accept, hand connections to with_gate.
class imap_server {
public:
    seastar::future<> start(uint16_t port);
    seastar::future<> stop();

private:
    seastar::future<> accept_loop();
    seastar::future<> handle_connection(seastar::connected_socket sock,
                                        seastar::socket_address remote);

    uint16_t _port = 1143;
    seastar::server_socket _listener;
    seastar::gate _gate;
    seastar::abort_source _as;
    authenticator _auth;
    bool _started = false;
};

} // namespace imap
