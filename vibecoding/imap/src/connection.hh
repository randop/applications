#pragma once

#include "auth/authenticator.hh"
#include "mailbox/store.hh"
#include "protocol/parser.hh"
#include "protocol/types.hh"

#include <seastar/core/abort_source.hh>
#include <seastar/core/future.hh>
#include <seastar/core/iostream.hh>
#include <seastar/net/api.hh>

#include <memory>
#include <optional>
#include <string>

namespace imap {

class imap_connection {
public:
    imap_connection(seastar::connected_socket sock,
                    seastar::socket_address remote,
                    authenticator& auth,
                    seastar::abort_source& as);

    seastar::future<> run();

private:
    seastar::future<> write_line(std::string line);
    seastar::future<> write_raw(std::string data);
    seastar::future<std::optional<std::string>> read_line();
    seastar::future<std::optional<std::string>> read_exact(std::size_t n);

    seastar::future<> handle_command(client_command cmd);
    seastar::future<> cmd_capability(const client_command& cmd);
    seastar::future<> cmd_noop(const client_command& cmd);
    seastar::future<> cmd_logout(const client_command& cmd);
    seastar::future<> cmd_login(const client_command& cmd);
    seastar::future<> cmd_select(const client_command& cmd, bool read_only);
    seastar::future<> cmd_create(const client_command& cmd);
    seastar::future<> cmd_delete(const client_command& cmd);
    seastar::future<> cmd_rename(const client_command& cmd);
    seastar::future<> cmd_subscribe(const client_command& cmd, bool on);
    seastar::future<> cmd_list(const client_command& cmd, bool lsub);
    seastar::future<> cmd_status(const client_command& cmd);
    seastar::future<> cmd_append(const client_command& cmd);
    seastar::future<> cmd_check(const client_command& cmd);
    seastar::future<> cmd_close(const client_command& cmd);
    seastar::future<> cmd_expunge(const client_command& cmd);
    seastar::future<> cmd_search(const client_command& cmd);
    seastar::future<> cmd_fetch(const client_command& cmd);
    seastar::future<> cmd_store(const client_command& cmd);
    seastar::future<> cmd_copy(const client_command& cmd);

    seastar::future<> emit_select_data(const mailbox& mbox, bool read_only);
    seastar::future<> fetch_one(uint32_t seq, const message& msg,
                                const std::vector<std::string>& items, bool peek);
    seastar::future<> apply_store(message& msg, std::string_view item,
                                  const std::vector<std::string>& flags, bool silent);

    [[nodiscard]] bool require_state(const client_command& cmd, session_state min);
    [[nodiscard]] mailbox* selected_mailbox();

    seastar::connected_socket _sock;
    seastar::socket_address _remote;
    seastar::input_stream<char> _in;
    seastar::output_stream<char> _out;
    authenticator& _auth;
    seastar::abort_source& _as;

    session_state _state = session_state::not_authenticated;
    std::unique_ptr<user_store> _store;
    std::string _selected_name;
    bool _selected_ro = false;
    bool _closing = false;
    std::string _line_leftover; // bytes after a consumed CRLF awaiting next read
};

} // namespace imap
