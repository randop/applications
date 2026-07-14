#pragma once

#include "protocol/types.hh"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace imap {

// Tokenize one complete IMAP client command line (literals already inlined as strings).
// Supports atoms, quoted strings, and parenthesized lists kept as a single arg token.
struct parse_result {
    bool ok = false;
    std::string error;
    client_command cmd;
};

// Returns nullopt if the line is incomplete because a literal is required.
// When a non-sync literal is needed, sets `literal_octets` and leaves partial parse empty.
struct line_scan {
    enum class status {
        complete,           // full command ready
        need_literal,       // client will send {n} octets next (after +)
        syntax_error,
    } st = status::complete;
    std::size_t literal_octets = 0;
    std::string error;
    client_command cmd;
};

// Scan a CRLF-terminated line for a trailing non-synchronizing or synchronizing literal.
// For barebones we accept `{n}` and `{n+}` ; after `{n}` server must send `+` continuation.
line_scan scan_command_line(std::string_view line);

// Parse a fully assembled command text (tag + body, no trailing CRLF).
parse_result parse_assembled_command(std::string_view text);

// Append literal body into the last incomplete literal placeholder in `acc`.
// `acc` holds the text before `{n}`, then we append the literal as a quoted-safe blob.
void append_literal_to_buffer(std::string& acc, std::string_view literal_bytes);

// Sequence-set helpers: "1", "1:3", "1,3:5", "*" (use exists for *)
// Returns 1-based sequence numbers sorted unique; empty on error.
std::vector<uint32_t> parse_sequence_set(std::string_view set, uint32_t max_seq, bool* ok);

// UID set against message UIDs in mailbox (sorted unique sequence numbers of matches).
std::vector<uint32_t> parse_uid_set_to_seq(std::string_view set,
                                           const std::vector<message>& messages,
                                           bool* ok);

// Split parenthesized FETCH/STORE items, e.g. "(FLAGS UID)" -> ["FLAGS","UID"]
std::vector<std::string> split_paren_list(std::string_view list);

// Unquote IMAP quoted-string or return atom as-is.
std::string unquote_astring(std::string_view token);

} // namespace imap
