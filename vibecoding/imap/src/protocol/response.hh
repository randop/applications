#pragma once

#include "protocol/types.hh"

#include <string>
#include <string_view>
#include <vector>

namespace imap {

// Build IMAP wire text (without final CRLF unless noted). Callers append \r\n when writing.

std::string untagged(std::string_view payload);
std::string tagged_ok(std::string_view tag, std::string_view text);
std::string tagged_no(std::string_view tag, std::string_view text);
std::string tagged_bad(std::string_view tag, std::string_view text);
std::string tagged_ok_code(std::string_view tag, std::string_view resp_code, std::string_view text);
std::string bye(std::string_view text);
std::string continuation();

// Quote an astring for responses.
std::string quote_astring(std::string_view s);

// FLAGS list for FETCH / permanent flags.
std::string format_flags(const message_flags& f, bool include_recent = true);

// ENVELOPE parenthesized structure (simplified address fields).
std::string format_envelope(const envelope& e);

// ADDRESS structure used inside ENVELOPE: (name adl mailbox host) or NIL.
std::string format_address(std::string_view email_or_empty);

// BODY[] / RFC822 style literal response fragment: name {n}\r\n<body>
std::string format_literal_body(std::string_view field_name, std::string_view data);

// CAPABILITY untagged line body (without "* ").
std::string capability_list();

// LIST / LSUB attributes.
std::string format_list_line(std::string_view name, bool noselect = false);

} // namespace imap
