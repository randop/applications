#include "connection.hh"

#include "protocol/response.hh"

#include <seastar/core/coroutine.hh>
#include <seastar/core/seastar.hh>
#include <seastar/coroutine/maybe_yield.hh>

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace imap {
namespace {

std::string strip_for_literal(std::string_view line) {
    auto s = trim_view(line);
    auto open = s.rfind('{');
    if (open == std::string_view::npos) {
        return std::string(s);
    }
    return std::string(trim_view(s.substr(0, open)));
}

std::vector<std::string> expand_fetch_items(const std::vector<std::string>& items) {
    std::vector<std::string> out;
    for (const auto& it : items) {
        if (it == "ALL") {
            out.insert(out.end(), {"FLAGS", "INTERNALDATE", "RFC822.SIZE", "ENVELOPE"});
        } else if (it == "FAST") {
            out.insert(out.end(), {"FLAGS", "INTERNALDATE", "RFC822.SIZE"});
        } else if (it == "FULL") {
            out.insert(out.end(),
                       {"FLAGS", "INTERNALDATE", "RFC822.SIZE", "ENVELOPE", "BODY"});
        } else {
            out.push_back(it);
        }
    }
    return out;
}

message_flags parse_flag_list(std::string_view paren) {
    message_flags f;
    auto items = split_paren_list(paren);
    for (auto& it : items) {
        // split_paren_list uppercases; restore system flag form.
        if (it == "\\ANSWERED" || it == "ANSWERED") {
            f.answered = true;
        } else if (it == "\\FLAGGED" || it == "FLAGGED") {
            f.flagged = true;
        } else if (it == "\\DELETED" || it == "DELETED") {
            f.deleted = true;
        } else if (it == "\\SEEN" || it == "SEEN") {
            f.seen = true;
        } else if (it == "\\DRAFT" || it == "DRAFT") {
            f.draft = true;
        } else if (it == "\\RECENT" || it == "RECENT") {
            f.recent = true;
        } else if (!it.empty() && it.front() != '\\') {
            f.add_keyword(it);
        } else {
            // Keep unknown system-like as keyword
            f.add_keyword(it);
        }
    }
    return f;
}

// Re-parse flags preserving backslash names from original token better.
std::vector<std::string> flag_names_from_list(std::string_view paren) {
    std::vector<std::string> names;
    auto s = trim_view(paren);
    if (s.size() >= 2 && s.front() == '(' && s.back() == ')') {
        s.remove_prefix(1);
        s.remove_suffix(1);
    }
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && s[i] == ' ') {
            ++i;
        }
        if (i >= s.size()) {
            break;
        }
        std::size_t j = i;
        while (j < s.size() && s[j] != ' ') {
            ++j;
        }
        names.emplace_back(s.substr(i, j - i));
        i = j;
    }
    return names;
}

} // namespace

imap_connection::imap_connection(seastar::connected_socket sock,
                                 seastar::socket_address remote,
                                 authenticator& auth,
                                 seastar::abort_source& as)
    : _sock(std::move(sock))
    , _remote(std::move(remote))
    , _in(_sock.input())
    , _out(_sock.output())
    , _auth(auth)
    , _as(as) {}

seastar::future<> imap_connection::write_line(std::string line) {
    line.append("\r\n");
    co_await _out.write(line);
    co_await _out.flush();
    co_return;
}

seastar::future<> imap_connection::write_raw(std::string data) {
    co_await _out.write(data);
    co_await _out.flush();
    co_return;
}

seastar::future<std::optional<std::string>> imap_connection::read_line() {
    constexpr size_t k_max = 64 * 1024;
    std::string line = std::move(_line_leftover);
    _line_leftover.clear();
    while (line.size() < k_max) {
        auto pos = line.find("\r\n");
        if (pos != std::string::npos) {
            std::string out = line.substr(0, pos);
            _line_leftover = line.substr(pos + 2);
            co_return out;
        }
        if (_as.abort_requested()) {
            co_return std::nullopt;
        }
        auto buf = co_await _in.read();
        if (buf.empty()) {
            co_return std::nullopt;
        }
        line.append(buf.get(), buf.size());
    }
    throw std::runtime_error("IMAP line too long");
}

seastar::future<std::optional<std::string>> imap_connection::read_exact(std::size_t n) {
    std::string out;
    out.reserve(n);
    if (!_line_leftover.empty()) {
        const auto take = std::min(n, _line_leftover.size());
        out.append(_line_leftover.data(), take);
        _line_leftover.erase(0, take);
    }
    while (out.size() < n) {
        if (_as.abort_requested()) {
            co_return std::nullopt;
        }
        auto buf = co_await _in.read_up_to(n - out.size());
        if (buf.empty()) {
            co_return std::nullopt;
        }
        out.append(buf.get(), buf.size());
    }
    co_return out;
}

bool imap_connection::require_state(const client_command& cmd, session_state min) {
    auto rank = [](session_state s) {
        switch (s) {
        case session_state::not_authenticated:
            return 0;
        case session_state::authenticated:
            return 1;
        case session_state::selected:
            return 2;
        case session_state::logout:
            return -1;
        }
        return -1;
    };
    if (rank(_state) < rank(min)) {
        return false;
    }
    (void)cmd;
    return true;
}

mailbox* imap_connection::selected_mailbox() {
    if (!_store || _selected_name.empty()) {
        return nullptr;
    }
    return _store->find(_selected_name);
}

seastar::future<> imap_connection::run() {
    try {
        co_await write_line(untagged("OK [" + capability_list() + "] IMAP4rev1 service ready"));

        std::string assemble;
        bool assembling_literal = false;
        std::size_t pending_literal = 0;

        while (!_closing && _state != session_state::logout && !_as.abort_requested()) {
            auto line_opt = co_await read_line();
            if (!line_opt) {
                break;
            }
            auto& line = *line_opt;

            if (assembling_literal) {
                // After + continuation, client sends raw octets then often CRLF of next cmd.
                // Our flow: need_literal -> write + -> read_exact(n) not via read_line.
                // This branch should not normally run; literal path below uses read_exact.
                assembling_literal = false;
                (void)pending_literal;
            }

            // Incremental assemble for multi-line literals:
            // First line may end with {n}; we request continuation and read_exact.
            std::string work = line;
            if (!assemble.empty()) {
                // Should not happen in simple flow
                work = assemble + " " + line;
                assemble.clear();
            }

            auto scan = scan_command_line(work);
            if (scan.st == line_scan::status::need_literal) {
                // Synchronizing literal: send + then read n octets, then continue.
                co_await write_line(continuation());
                auto lit = co_await read_exact(scan.literal_octets);
                if (!lit) {
                    break;
                }
                // Optional CRLF after literal in some clients — not always present.
                // IMAP: literal is exact n octets; next command line follows.
                std::string acc = strip_for_literal(work);
                append_literal_to_buffer(acc, *lit);
                // May need more literals; loop by re-scanning.
                while (true) {
                    auto sc2 = scan_command_line(acc);
                    if (sc2.st == line_scan::status::need_literal) {
                        co_await write_line(continuation());
                        auto lit2 = co_await read_exact(sc2.literal_octets);
                        if (!lit2) {
                            co_return;
                        }
                        acc = strip_for_literal(acc);
                        append_literal_to_buffer(acc, *lit2);
                        continue;
                    }
                    if (sc2.st == line_scan::status::syntax_error) {
                        co_await write_line(tagged_bad("*", sc2.error));
                        break;
                    }
                    if (sc2.cmd.kind != command_kind::empty) {
                        co_await handle_command(std::move(sc2.cmd));
                    }
                    break;
                }
                continue;
            }

            if (scan.st == line_scan::status::syntax_error) {
                // Try to extract tag for BAD
                auto sp = work.find(' ');
                std::string tag = sp == std::string::npos ? "*" : work.substr(0, sp);
                co_await write_line(tagged_bad(tag, scan.error));
                continue;
            }

            if (scan.cmd.kind == command_kind::empty) {
                continue;
            }
            co_await handle_command(std::move(scan.cmd));
            co_await seastar::coroutine::maybe_yield();
        }
    } catch (const std::exception& e) {
        // Best-effort BYE; ignore write failures.
        try {
            co_await write_line(bye(std::string("Server error: ") + e.what()));
        } catch (...) {
        }
    }

    try {
        co_await _out.flush();
        co_await _out.close();
    } catch (...) {
    }
    try {
        co_await _in.close();
    } catch (...) {
    }
    co_return;
}

seastar::future<> imap_connection::handle_command(client_command cmd) {
    switch (cmd.kind) {
    case command_kind::capability:
        co_await cmd_capability(cmd);
        break;
    case command_kind::noop:
        co_await cmd_noop(cmd);
        break;
    case command_kind::logout:
        co_await cmd_logout(cmd);
        break;
    case command_kind::login:
        co_await cmd_login(cmd);
        break;
    case command_kind::select:
        co_await cmd_select(cmd, false);
        break;
    case command_kind::examine:
        co_await cmd_select(cmd, true);
        break;
    case command_kind::create:
        co_await cmd_create(cmd);
        break;
    case command_kind::delete_mbox:
        co_await cmd_delete(cmd);
        break;
    case command_kind::rename:
        co_await cmd_rename(cmd);
        break;
    case command_kind::subscribe:
        co_await cmd_subscribe(cmd, true);
        break;
    case command_kind::unsubscribe:
        co_await cmd_subscribe(cmd, false);
        break;
    case command_kind::list:
        co_await cmd_list(cmd, false);
        break;
    case command_kind::lsub:
        co_await cmd_list(cmd, true);
        break;
    case command_kind::status:
        co_await cmd_status(cmd);
        break;
    case command_kind::append:
        co_await cmd_append(cmd);
        break;
    case command_kind::check:
        co_await cmd_check(cmd);
        break;
    case command_kind::close:
        co_await cmd_close(cmd);
        break;
    case command_kind::expunge:
        co_await cmd_expunge(cmd);
        break;
    case command_kind::search:
        co_await cmd_search(cmd);
        break;
    case command_kind::fetch:
        co_await cmd_fetch(cmd);
        break;
    case command_kind::store:
        co_await cmd_store(cmd);
        break;
    case command_kind::copy:
        co_await cmd_copy(cmd);
        break;
    case command_kind::uid:
        co_await write_line(tagged_bad(cmd.tag, "UID subcommand incomplete"));
        break;
    case command_kind::unknown:
        co_await write_line(tagged_bad(cmd.tag, "Unknown command"));
        break;
    case command_kind::empty:
        break;
    }
    co_return;
}

seastar::future<> imap_connection::cmd_capability(const client_command& cmd) {
    co_await write_line(untagged(capability_list()));
    co_await write_line(tagged_ok(cmd.tag, "CAPABILITY completed"));
    co_return;
}

seastar::future<> imap_connection::cmd_noop(const client_command& cmd) {
    co_await write_line(tagged_ok(cmd.tag, "NOOP completed"));
    co_return;
}

seastar::future<> imap_connection::cmd_logout(const client_command& cmd) {
    co_await write_line(bye("Logging out"));
    co_await write_line(tagged_ok(cmd.tag, "LOGOUT completed"));
    _state = session_state::logout;
    _closing = true;
    co_return;
}

seastar::future<> imap_connection::cmd_login(const client_command& cmd) {
    if (_state != session_state::not_authenticated) {
        co_await write_line(tagged_bad(cmd.tag, "Already authenticated"));
        co_return;
    }
    if (cmd.args.size() < 2) {
        co_await write_line(tagged_bad(cmd.tag, "LOGIN requires username and password"));
        co_return;
    }
    auto user = unquote_astring(cmd.args[0]);
    auto pass = unquote_astring(cmd.args[1]);
    if (!_auth.check(user, pass)) {
        co_await write_line(tagged_no(cmd.tag, "LOGIN failed"));
        co_return;
    }
    _store = std::make_unique<user_store>(std::move(user));
    _state = session_state::authenticated;
    co_await write_line(tagged_ok(cmd.tag, "LOGIN completed"));
    co_return;
}

seastar::future<> imap_connection::emit_select_data(const mailbox& mbox, bool read_only) {
    co_await write_line(untagged("FLAGS (\\Answered \\Flagged \\Deleted \\Seen \\Draft)"));
    co_await write_line(untagged(
        "OK [PERMANENTFLAGS (\\Answered \\Flagged \\Deleted \\Seen \\Draft \\*)] Limited"));
    co_await write_line(untagged(std::to_string(mbox.exists()) + " EXISTS"));
    co_await write_line(untagged(std::to_string(mbox.recent_count()) + " RECENT"));
    auto unseen = mbox.unseen_first();
    if (unseen > 0) {
        co_await write_line(untagged("OK [UNSEEN " + std::to_string(unseen) + "] Message unseen"));
    }
    co_await write_line(
        untagged("OK [UIDVALIDITY " + std::to_string(mbox.uidvalidity) + "] UIDs valid"));
    co_await write_line(
        untagged("OK [UIDNEXT " + std::to_string(mbox.uidnext) + "] Predicted next UID"));
    (void)read_only;
    co_return;
}

seastar::future<> imap_connection::cmd_select(const client_command& cmd, bool read_only) {
    if (!require_state(cmd, session_state::authenticated)) {
        co_await write_line(tagged_no(cmd.tag, "Not authenticated"));
        co_return;
    }
    if (cmd.args.empty()) {
        co_await write_line(tagged_bad(cmd.tag, "Mailbox name required"));
        co_return;
    }
    auto name = unquote_astring(cmd.args[0]);
    auto* mbox = _store->find(name);
    if (!mbox) {
        co_await write_line(tagged_no(cmd.tag, "Mailbox does not exist"));
        co_return;
    }
    _selected_name = mbox->name;
    _selected_ro = read_only;
    _state = session_state::selected;
    // Clear \Recent on SELECT (session ownership — barebones: clear all recent).
    for (auto& m : mbox->messages) {
        m.flags.recent = false;
    }
    co_await emit_select_data(*mbox, read_only);
    if (read_only) {
        co_await write_line(tagged_ok_code(cmd.tag, "READ-ONLY", "EXAMINE completed"));
    } else {
        co_await write_line(tagged_ok_code(cmd.tag, "READ-WRITE", "SELECT completed"));
    }
    co_return;
}

seastar::future<> imap_connection::cmd_create(const client_command& cmd) {
    if (!require_state(cmd, session_state::authenticated)) {
        co_await write_line(tagged_no(cmd.tag, "Not authenticated"));
        co_return;
    }
    if (cmd.args.empty()) {
        co_await write_line(tagged_bad(cmd.tag, "Mailbox name required"));
        co_return;
    }
    auto name = unquote_astring(cmd.args[0]);
    if (!_store->create(name)) {
        co_await write_line(tagged_no(cmd.tag, "CREATE failed"));
        co_return;
    }
    co_await write_line(tagged_ok(cmd.tag, "CREATE completed"));
    co_return;
}

seastar::future<> imap_connection::cmd_delete(const client_command& cmd) {
    if (!require_state(cmd, session_state::authenticated)) {
        co_await write_line(tagged_no(cmd.tag, "Not authenticated"));
        co_return;
    }
    if (cmd.args.empty()) {
        co_await write_line(tagged_bad(cmd.tag, "Mailbox name required"));
        co_return;
    }
    auto name = unquote_astring(cmd.args[0]);
    if (_selected_name == name || to_upper(name) == "INBOX") {
        // Deselect if needed
        if (_selected_name == name || (_selected_name == "INBOX" && to_upper(name) == "INBOX")) {
            if (to_upper(name) == "INBOX") {
                co_await write_line(tagged_no(cmd.tag, "Cannot delete INBOX"));
                co_return;
            }
        }
    }
    if (to_upper(name) == "INBOX") {
        co_await write_line(tagged_no(cmd.tag, "Cannot delete INBOX"));
        co_return;
    }
    if (_selected_name == name) {
        _selected_name.clear();
        _state = session_state::authenticated;
    }
    if (!_store->remove(name)) {
        co_await write_line(tagged_no(cmd.tag, "DELETE failed"));
        co_return;
    }
    co_await write_line(tagged_ok(cmd.tag, "DELETE completed"));
    co_return;
}

seastar::future<> imap_connection::cmd_rename(const client_command& cmd) {
    if (!require_state(cmd, session_state::authenticated)) {
        co_await write_line(tagged_no(cmd.tag, "Not authenticated"));
        co_return;
    }
    if (cmd.args.size() < 2) {
        co_await write_line(tagged_bad(cmd.tag, "RENAME requires two mailbox names"));
        co_return;
    }
    auto from = unquote_astring(cmd.args[0]);
    auto to = unquote_astring(cmd.args[1]);
    if (!_store->rename(from, to)) {
        co_await write_line(tagged_no(cmd.tag, "RENAME failed"));
        co_return;
    }
    if (_selected_name == from) {
        _selected_name = to;
    }
    co_await write_line(tagged_ok(cmd.tag, "RENAME completed"));
    co_return;
}

seastar::future<> imap_connection::cmd_subscribe(const client_command& cmd, bool on) {
    if (!require_state(cmd, session_state::authenticated)) {
        co_await write_line(tagged_no(cmd.tag, "Not authenticated"));
        co_return;
    }
    if (cmd.args.empty()) {
        co_await write_line(tagged_bad(cmd.tag, "Mailbox name required"));
        co_return;
    }
    auto name = unquote_astring(cmd.args[0]);
    if (!_store->subscribe(name, on)) {
        co_await write_line(tagged_no(cmd.tag, on ? "SUBSCRIBE failed" : "UNSUBSCRIBE failed"));
        co_return;
    }
    co_await write_line(tagged_ok(cmd.tag, on ? "SUBSCRIBE completed" : "UNSUBSCRIBE completed"));
    co_return;
}

seastar::future<> imap_connection::cmd_list(const client_command& cmd, bool lsub) {
    if (!require_state(cmd, session_state::authenticated)) {
        co_await write_line(tagged_no(cmd.tag, "Not authenticated"));
        co_return;
    }
    if (cmd.args.size() < 2) {
        co_await write_line(tagged_bad(cmd.tag, "LIST requires reference and mailbox"));
        co_return;
    }
    auto reference = unquote_astring(cmd.args[0]);
    auto mbox_pat = unquote_astring(cmd.args[1]);
    auto names = _store->list_names(reference, mbox_pat);
    for (const auto& n : names) {
        auto* m = _store->find(n);
        if (lsub && m && !m->subscribed) {
            continue;
        }
        std::string payload = lsub ? "LSUB () \"/\" " : "LIST () \"/\" ";
        payload.append(quote_astring(n));
        co_await write_line(untagged(payload));
        co_await seastar::coroutine::maybe_yield();
    }
    co_await write_line(tagged_ok(cmd.tag, lsub ? "LSUB completed" : "LIST completed"));
    co_return;
}

seastar::future<> imap_connection::cmd_status(const client_command& cmd) {
    if (!require_state(cmd, session_state::authenticated)) {
        co_await write_line(tagged_no(cmd.tag, "Not authenticated"));
        co_return;
    }
    if (cmd.args.size() < 2) {
        co_await write_line(tagged_bad(cmd.tag, "STATUS requires mailbox and item list"));
        co_return;
    }
    auto name = unquote_astring(cmd.args[0]);
    auto* mbox = _store->find(name);
    if (!mbox) {
        co_await write_line(tagged_no(cmd.tag, "Mailbox does not exist"));
        co_return;
    }
    auto items = split_paren_list(cmd.args[1]);
    std::string body = "STATUS ";
    body.append(quote_astring(mbox->name));
    body.append(" (");
    bool first = true;
    for (const auto& it : items) {
        if (!first) {
            body.push_back(' ');
        }
        first = false;
        if (it == "MESSAGES") {
            body.append("MESSAGES ");
            body.append(std::to_string(mbox->exists()));
        } else if (it == "RECENT") {
            body.append("RECENT ");
            body.append(std::to_string(mbox->recent_count()));
        } else if (it == "UIDNEXT") {
            body.append("UIDNEXT ");
            body.append(std::to_string(mbox->uidnext));
        } else if (it == "UIDVALIDITY") {
            body.append("UIDVALIDITY ");
            body.append(std::to_string(mbox->uidvalidity));
        } else if (it == "UNSEEN") {
            std::size_t unseen = 0;
            for (const auto& m : mbox->messages) {
                if (!m.flags.seen) {
                    ++unseen;
                }
            }
            body.append("UNSEEN ");
            body.append(std::to_string(unseen));
        } else {
            body.append(it);
            body.append(" 0");
        }
    }
    body.push_back(')');
    co_await write_line(untagged(body));
    co_await write_line(tagged_ok(cmd.tag, "STATUS completed"));
    co_return;
}

seastar::future<> imap_connection::cmd_append(const client_command& cmd) {
    if (!require_state(cmd, session_state::authenticated)) {
        co_await write_line(tagged_no(cmd.tag, "Not authenticated"));
        co_return;
    }
    // APPEND mailbox [flags] [date-time] message-literal
    // After full parse, last arg is message body (quoted from literal assembly).
    if (cmd.args.size() < 2) {
        co_await write_line(tagged_bad(cmd.tag, "APPEND syntax error"));
        co_return;
    }
    auto mbox_name = unquote_astring(cmd.args[0]);
    message_flags flags;
    std::string date;
    std::string raw;
    // Parse optional flags and date from middle args.
    std::size_t i = 1;
    if (i < cmd.args.size() && !cmd.args[i].empty() && cmd.args[i].front() == '(') {
        flags = parse_flag_list(cmd.args[i]);
        ++i;
    }
    if (i + 1 < cmd.args.size()) {
        // date-time then literal
        date = unquote_astring(cmd.args[i]);
        ++i;
    }
    if (i >= cmd.args.size()) {
        co_await write_line(tagged_bad(cmd.tag, "APPEND missing message"));
        co_return;
    }
    raw = unquote_astring(cmd.args.back());
    auto uid = _store->append(mbox_name, flags, date, std::move(raw));
    if (!uid) {
        co_await write_line(tagged_no(cmd.tag, "APPEND failed (no such mailbox)"));
        co_return;
    }
    // If selected mailbox, EXISTS update optional.
    if (auto* sel = selected_mailbox(); sel && sel->name == mbox_name) {
        co_await write_line(untagged(std::to_string(sel->exists()) + " EXISTS"));
    }
    {
        auto* mbox = _store->find(mbox_name);
        const auto uidvalidity = mbox ? mbox->uidvalidity : 1u;
        co_await write_line(tagged_ok_code(
            cmd.tag, "APPENDUID " + std::to_string(uidvalidity) + " " + std::to_string(*uid),
            "APPEND completed"));
    }
    co_return;
}

seastar::future<> imap_connection::cmd_check(const client_command& cmd) {
    if (!require_state(cmd, session_state::selected)) {
        co_await write_line(tagged_no(cmd.tag, "Not selected"));
        co_return;
    }
    co_await write_line(tagged_ok(cmd.tag, "CHECK completed"));
    co_return;
}

seastar::future<> imap_connection::cmd_close(const client_command& cmd) {
    if (!require_state(cmd, session_state::selected)) {
        co_await write_line(tagged_no(cmd.tag, "Not selected"));
        co_return;
    }
    auto* mbox = selected_mailbox();
    if (mbox && !_selected_ro) {
        (void)_store->expunge(*mbox);
    }
    _selected_name.clear();
    _state = session_state::authenticated;
    co_await write_line(tagged_ok(cmd.tag, "CLOSE completed"));
    co_return;
}

seastar::future<> imap_connection::cmd_expunge(const client_command& cmd) {
    if (!require_state(cmd, session_state::selected)) {
        co_await write_line(tagged_no(cmd.tag, "Not selected"));
        co_return;
    }
    if (_selected_ro) {
        co_await write_line(tagged_no(cmd.tag, "Mailbox is read-only"));
        co_return;
    }
    auto* mbox = selected_mailbox();
    if (!mbox) {
        co_await write_line(tagged_no(cmd.tag, "No mailbox"));
        co_return;
    }
    // RFC: expunge with renumbering — send EXPUNGE for each, adjusting.
    // Algorithm: repeatedly find first \Deleted and expunge it as current seq.
    std::size_t i = 0;
    while (i < mbox->messages.size()) {
        if (mbox->messages[i].flags.deleted) {
            co_await write_line(untagged(std::to_string(i + 1) + " EXPUNGE"));
            mbox->messages.erase(mbox->messages.begin()
                                 + static_cast<std::ptrdiff_t>(i));
            co_await seastar::coroutine::maybe_yield();
        } else {
            ++i;
        }
    }
    co_await write_line(tagged_ok(cmd.tag, "EXPUNGE completed"));
    co_return;
}

seastar::future<> imap_connection::cmd_search(const client_command& cmd) {
    if (!require_state(cmd, session_state::selected)) {
        co_await write_line(tagged_no(cmd.tag, "Not selected"));
        co_return;
    }
    auto* mbox = selected_mailbox();
    if (!mbox) {
        co_await write_line(tagged_no(cmd.tag, "No mailbox"));
        co_return;
    }

    // Barebones keys: ALL, SEEN, UNSEEN, FLAGGED, DELETED, NEW, OLD, RECENT, NOT <key>, UID <set>
    // Default ALL if no criteria.
    std::vector<std::string> criteria;
    for (const auto& a : cmd.args) {
        criteria.push_back(to_upper(a));
    }
    if (criteria.empty()) {
        criteria.push_back("ALL");
    }
    // CHARSET optional: SEARCH CHARSET UTF-8 ...
    std::size_t idx = 0;
    if (criteria.size() >= 2 && criteria[0] == "CHARSET") {
        idx = 2;
    }

    auto match_one = [&](const message& m, std::size_t seq) -> bool {
        (void)seq;
        // Evaluate criteria as AND of tokens (very simplified).
        for (std::size_t i = idx; i < criteria.size(); ++i) {
            const auto& c = criteria[i];
            if (c == "ALL") {
                continue;
            }
            if (c == "SEEN") {
                if (!m.flags.seen) {
                    return false;
                }
            } else if (c == "UNSEEN") {
                if (m.flags.seen) {
                    return false;
                }
            } else if (c == "FLAGGED") {
                if (!m.flags.flagged) {
                    return false;
                }
            } else if (c == "DELETED") {
                if (!m.flags.deleted) {
                    return false;
                }
            } else if (c == "RECENT" || c == "NEW") {
                if (!m.flags.recent) {
                    return false;
                }
            } else if (c == "OLD") {
                if (m.flags.recent) {
                    return false;
                }
            } else if (c == "NOT" && i + 1 < criteria.size()) {
                // handled by inverting next — skip simplistic
                ++i;
                const auto& n = criteria[i];
                if (n == "SEEN" && m.flags.seen) {
                    return false;
                }
                if (n == "FLAGGED" && m.flags.flagged) {
                    return false;
                }
            } else if (c == "UID" && i + 1 < cmd.args.size()) {
                // Use original case args for set
                bool ok = true;
                auto seqs = parse_uid_set_to_seq(cmd.args[i + 1], mbox->messages, &ok);
                ++i;
                if (!ok) {
                    return false;
                }
                bool found = false;
                for (uint32_t s : seqs) {
                    if (s == seq) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    return false;
                }
            } else if (std::isdigit(static_cast<unsigned char>(c.front())) || c.front() == '*') {
                bool ok = true;
                auto seqs = parse_sequence_set(c, static_cast<uint32_t>(mbox->exists()), &ok);
                if (!ok) {
                    return false;
                }
                bool found = false;
                for (uint32_t s : seqs) {
                    if (s == seq) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    return false;
                }
            } else {
                // Unknown key: ignore for barebones (match)
            }
        }
        return true;
    };

    std::string result = "SEARCH";
    for (std::size_t i = 0; i < mbox->messages.size(); ++i) {
        uint32_t seq = static_cast<uint32_t>(i + 1);
        const auto& m = mbox->messages[i];
        bool hit = match_one(m, seq);
        if (cmd.uid_mode) {
            // UID SEARCH returns UIDs
            if (hit) {
                result.push_back(' ');
                result.append(std::to_string(m.uid));
            }
        } else if (hit) {
            result.push_back(' ');
            result.append(std::to_string(seq));
        }
        if ((i & 0x3f) == 0) {
            co_await seastar::coroutine::maybe_yield();
        }
    }
    co_await write_line(untagged(result));
    co_await write_line(tagged_ok(cmd.tag, "SEARCH completed"));
    co_return;
}

seastar::future<> imap_connection::fetch_one(uint32_t seq, const message& msg,
                                             const std::vector<std::string>& items, bool peek) {
    std::string body = std::to_string(seq);
    body.append(" FETCH (");
    bool first = true;
    auto add_sp = [&] {
        if (!first) {
            body.push_back(' ');
        }
        first = false;
    };

    bool saw_body = false;
    for (const auto& raw_item : items) {
        auto item = raw_item;
        // BODY.PEEK[...] vs BODY[...]
        bool item_peek = peek;
        if (item.rfind("BODY.PEEK", 0) == 0) {
            item_peek = true;
            item.replace(0, 9, "BODY");
        }
        if (item == "BODY" || item.rfind("BODY[", 0) == 0 || item == "RFC822"
            || item == "RFC822.HEADER" || item == "RFC822.TEXT") {
            saw_body = saw_body || !item_peek;
        }

        add_sp();
        if (item == "FLAGS") {
            body.append("FLAGS ");
            body.append(format_flags(msg.flags));
        } else if (item == "UID") {
            body.append("UID ");
            body.append(std::to_string(msg.uid));
        } else if (item == "INTERNALDATE") {
            body.append("INTERNALDATE ");
            body.append(quote_astring(msg.internaldate));
        } else if (item == "RFC822.SIZE" || item == "RFC822.SIZE") {
            body.append("RFC822.SIZE ");
            body.append(std::to_string(msg.raw.size()));
        } else if (item == "ENVELOPE") {
            body.append("ENVELOPE ");
            body.append(format_envelope(msg.env));
        } else if (item == "BODY" || item == "BODYSTRUCTURE") {
            // Minimal one-part text structure
            body.append(item);
            body.append(" (\"TEXT\" \"PLAIN\" (\"CHARSET\" \"UTF-8\") NIL NIL \"7BIT\" ");
            body.append(std::to_string(msg.raw.size()));
            body.append(" ");
            // line count rough
            body.append(std::to_string(std::count(msg.raw.begin(), msg.raw.end(), '\n')));
            body.append(")");
        } else if (item == "RFC822" || item == "BODY[]" || item == "BODY.PEEK[]") {
            body.append(format_literal_body(item == "RFC822" ? "RFC822" : "BODY[]", msg.raw));
        } else if (item == "RFC822.HEADER" || item == "BODY[HEADER]") {
            auto end = msg.raw.find("\r\n\r\n");
            std::string_view hdr = end == std::string::npos ? std::string_view(msg.raw)
                                                            : std::string_view(msg.raw).substr(0, end + 4);
            body.append(format_literal_body(item == "RFC822.HEADER" ? "RFC822.HEADER" : "BODY[HEADER]",
                                            hdr));
        } else if (item == "RFC822.TEXT" || item == "BODY[TEXT]") {
            auto end = msg.raw.find("\r\n\r\n");
            std::string_view txt = end == std::string::npos
                                       ? std::string_view{}
                                       : std::string_view(msg.raw).substr(end + 4);
            body.append(
                format_literal_body(item == "RFC822.TEXT" ? "RFC822.TEXT" : "BODY[TEXT]", txt));
        } else if (item.rfind("BODY[", 0) == 0) {
            // Generic BODY section -> full message
            body.append(format_literal_body("BODY[]", msg.raw));
        } else {
            body.append(item);
            body.append(" NIL");
        }
    }
    body.push_back(')');

    // Implicit \Seen for non-peek body fetches
    if (saw_body && !peek) {
        // flags already formatted; mutate message — caller passes const; need non-const
    }
    co_await write_line(untagged(body));
    co_return;
}

seastar::future<> imap_connection::cmd_fetch(const client_command& cmd) {
    if (!require_state(cmd, session_state::selected)) {
        co_await write_line(tagged_no(cmd.tag, "Not selected"));
        co_return;
    }
    auto* mbox = selected_mailbox();
    if (!mbox || cmd.args.size() < 2) {
        co_await write_line(tagged_bad(cmd.tag, "FETCH requires sequence set and items"));
        co_return;
    }

    bool ok = true;
    std::vector<uint32_t> seqs;
    if (cmd.uid_mode) {
        seqs = parse_uid_set_to_seq(cmd.args[0], mbox->messages, &ok);
    } else {
        seqs = parse_sequence_set(cmd.args[0], static_cast<uint32_t>(mbox->exists()), &ok);
    }
    if (!ok) {
        co_await write_line(tagged_bad(cmd.tag, "Invalid sequence set"));
        co_return;
    }

    std::vector<std::string> items;
    if (cmd.args[1].size() >= 2 && cmd.args[1].front() == '(') {
        items = split_paren_list(cmd.args[1]);
    } else {
        items.push_back(to_upper(cmd.args[1]));
    }
    // UID FETCH always includes UID
    if (cmd.uid_mode) {
        bool has_uid = false;
        for (const auto& it : items) {
            if (it == "UID") {
                has_uid = true;
                break;
            }
        }
        if (!has_uid) {
            items.insert(items.begin(), "UID");
        }
    }
    items = expand_fetch_items(items);

    for (uint32_t seq : seqs) {
        if (seq == 0 || seq > mbox->messages.size()) {
            continue;
        }
        auto& msg = mbox->messages[seq - 1];
        bool peek = true;
        for (const auto& it : items) {
            if (it == "RFC822" || it == "BODY" || (it.rfind("BODY[", 0) == 0 && it.rfind("BODY.PEEK", 0) != 0)) {
                if (it.rfind("BODY.PEEK", 0) != 0 && it != "BODYSTRUCTURE") {
                    // BODY without PEEK sets seen — macros BODY in FULL sets structure only
                    if (it == "RFC822" || it.rfind("BODY[", 0) == 0) {
                        peek = false;
                    }
                }
            }
        }
        // Detect non-peek body sections more carefully
        peek = true;
        for (const auto& it : items) {
            if (it == "RFC822" || it == "RFC822.TEXT" || it == "RFC822.HEADER") {
                peek = false;
            }
            if (it.rfind("BODY[", 0) == 0) {
                peek = false;
            }
            if (it.rfind("BODY.PEEK", 0) == 0) {
                peek = true;
            }
        }
        if (!peek && !_selected_ro) {
            msg.flags.seen = true;
        }
        co_await fetch_one(seq, msg, items, peek);
        co_await seastar::coroutine::maybe_yield();
    }
    co_await write_line(tagged_ok(cmd.tag, "FETCH completed"));
    co_return;
}

seastar::future<> imap_connection::apply_store(message& msg, std::string_view item,
                                               const std::vector<std::string>& flags, bool silent) {
    (void)silent;
    auto apply_flag = [&](std::string_view name, bool on) {
        if (name == "\\Answered" || name == "\\ANSWERED") {
            msg.flags.answered = on;
        } else if (name == "\\Flagged" || name == "\\FLAGGED") {
            msg.flags.flagged = on;
        } else if (name == "\\Deleted" || name == "\\DELETED") {
            msg.flags.deleted = on;
        } else if (name == "\\Seen" || name == "\\SEEN") {
            msg.flags.seen = on;
        } else if (name == "\\Draft" || name == "\\DRAFT") {
            msg.flags.draft = on;
        } else if (!name.empty() && name.front() != '\\') {
            if (on) {
                msg.flags.add_keyword(std::string(name));
            } else {
                msg.flags.remove_keyword(name);
            }
        }
    };

    std::string u = to_upper(item);
    if (u == "FLAGS" || u == "FLAGS.SILENT") {
        msg.flags.answered = msg.flags.flagged = msg.flags.deleted = msg.flags.seen
            = msg.flags.draft = false;
        msg.flags.keywords.clear();
        for (const auto& f : flags) {
            apply_flag(f, true);
        }
    } else if (u == "+FLAGS" || u == "+FLAGS.SILENT") {
        for (const auto& f : flags) {
            apply_flag(f, true);
        }
    } else if (u == "-FLAGS" || u == "-FLAGS.SILENT") {
        for (const auto& f : flags) {
            apply_flag(f, false);
        }
    }
    co_return;
}

seastar::future<> imap_connection::cmd_store(const client_command& cmd) {
    if (!require_state(cmd, session_state::selected)) {
        co_await write_line(tagged_no(cmd.tag, "Not selected"));
        co_return;
    }
    if (_selected_ro) {
        co_await write_line(tagged_no(cmd.tag, "Mailbox is read-only"));
        co_return;
    }
    auto* mbox = selected_mailbox();
    if (!mbox || cmd.args.size() < 3) {
        co_await write_line(tagged_bad(cmd.tag, "STORE requires set, item, flags"));
        co_return;
    }
    bool ok = true;
    std::vector<uint32_t> seqs;
    if (cmd.uid_mode) {
        seqs = parse_uid_set_to_seq(cmd.args[0], mbox->messages, &ok);
    } else {
        seqs = parse_sequence_set(cmd.args[0], static_cast<uint32_t>(mbox->exists()), &ok);
    }
    if (!ok) {
        co_await write_line(tagged_bad(cmd.tag, "Invalid sequence set"));
        co_return;
    }
    auto item = cmd.args[1];
    bool silent = to_upper(item).find(".SILENT") != std::string::npos;
    auto flags = flag_names_from_list(cmd.args[2]);

    for (uint32_t seq : seqs) {
        if (seq == 0 || seq > mbox->messages.size()) {
            continue;
        }
        auto& msg = mbox->messages[seq - 1];
        co_await apply_store(msg, item, flags, silent);
        if (!silent) {
            std::string payload = std::to_string(seq);
            payload.append(" FETCH (FLAGS ");
            payload.append(format_flags(msg.flags));
            if (cmd.uid_mode) {
                payload.append(" UID ");
                payload.append(std::to_string(msg.uid));
            }
            payload.push_back(')');
            co_await write_line(untagged(payload));
        }
        co_await seastar::coroutine::maybe_yield();
    }
    co_await write_line(tagged_ok(cmd.tag, "STORE completed"));
    co_return;
}

seastar::future<> imap_connection::cmd_copy(const client_command& cmd) {
    if (!require_state(cmd, session_state::selected)) {
        co_await write_line(tagged_no(cmd.tag, "Not selected"));
        co_return;
    }
    auto* mbox = selected_mailbox();
    if (!mbox || cmd.args.size() < 2) {
        co_await write_line(tagged_bad(cmd.tag, "COPY requires set and mailbox"));
        co_return;
    }
    bool ok = true;
    std::vector<uint32_t> seqs;
    if (cmd.uid_mode) {
        seqs = parse_uid_set_to_seq(cmd.args[0], mbox->messages, &ok);
    } else {
        seqs = parse_sequence_set(cmd.args[0], static_cast<uint32_t>(mbox->exists()), &ok);
    }
    if (!ok) {
        co_await write_line(tagged_bad(cmd.tag, "Invalid sequence set"));
        co_return;
    }
    auto dest = unquote_astring(cmd.args[1]);
    if (!_store->copy_messages(*mbox, seqs, dest)) {
        co_await write_line(tagged_no(cmd.tag, "[TRYCREATE] COPY failed"));
        co_return;
    }
    co_await write_line(tagged_ok(cmd.tag, "COPY completed"));
    co_return;
}

} // namespace imap
