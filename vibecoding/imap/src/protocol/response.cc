#include "protocol/response.hh"

#include <sstream>

namespace imap {

std::string untagged(std::string_view payload) {
    std::string s = "* ";
    s.append(payload);
    return s;
}

std::string tagged_ok(std::string_view tag, std::string_view text) {
    std::string s;
    s.reserve(tag.size() + text.size() + 8);
    s.append(tag);
    s.append(" OK ");
    s.append(text);
    return s;
}

std::string tagged_no(std::string_view tag, std::string_view text) {
    std::string s;
    s.append(tag);
    s.append(" NO ");
    s.append(text);
    return s;
}

std::string tagged_bad(std::string_view tag, std::string_view text) {
    std::string s;
    s.append(tag);
    s.append(" BAD ");
    s.append(text);
    return s;
}

std::string tagged_ok_code(std::string_view tag, std::string_view resp_code, std::string_view text) {
    std::string s;
    s.append(tag);
    s.append(" OK [");
    s.append(resp_code);
    s.append("] ");
    s.append(text);
    return s;
}

std::string bye(std::string_view text) {
    std::string s = "* BYE ";
    s.append(text);
    return s;
}

std::string continuation() {
    return "+ Ready for additional command text";
}

std::string quote_astring(std::string_view s) {
    std::string out;
    out.push_back('"');
    for (char c : s) {
        if (c == '\\' || c == '"') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

std::string format_flags(const message_flags& f, bool include_recent) {
    std::string s = "(";
    bool first = true;
    auto add = [&](std::string_view flag) {
        if (!first) {
            s.push_back(' ');
        }
        first = false;
        s.append(flag);
    };
    if (f.answered) {
        add("\\Answered");
    }
    if (f.flagged) {
        add("\\Flagged");
    }
    if (f.deleted) {
        add("\\Deleted");
    }
    if (f.seen) {
        add("\\Seen");
    }
    if (f.draft) {
        add("\\Draft");
    }
    if (include_recent && f.recent) {
        add("\\Recent");
    }
    for (const auto& k : f.keywords) {
        add(k);
    }
    s.push_back(')');
    return s;
}

std::string format_address(std::string_view email_or_empty) {
    if (email_or_empty.empty()) {
        return "NIL";
    }
    // Split local@domain
    auto at = email_or_empty.find('@');
    std::string mailbox;
    std::string host;
    if (at == std::string_view::npos) {
        mailbox = std::string(email_or_empty);
        host = "localhost";
    } else {
        mailbox = std::string(email_or_empty.substr(0, at));
        host = std::string(email_or_empty.substr(at + 1));
    }
    std::string s = "(NIL NIL ";
    s.append(quote_astring(mailbox));
    s.push_back(' ');
    s.append(quote_astring(host));
    s.push_back(')');
    return s;
}

std::string format_envelope(const envelope& e) {
    // (date subject from sender reply-to to cc bcc in-reply-to message-id)
    auto addr_list = [](std::string_view email) -> std::string {
        if (email.empty()) {
            return "NIL";
        }
        return "(" + format_address(email) + ")";
    };

    std::string s = "(";
    s.append(e.date.empty() ? "NIL" : quote_astring(e.date));
    s.push_back(' ');
    s.append(e.subject.empty() ? "NIL" : quote_astring(e.subject));
    s.push_back(' ');
    s.append(addr_list(e.from));
    s.push_back(' ');
    s.append(addr_list(e.sender.empty() ? e.from : e.sender));
    s.push_back(' ');
    s.append(addr_list(e.reply_to.empty() ? e.from : e.reply_to));
    s.push_back(' ');
    s.append(addr_list(e.to));
    s.push_back(' ');
    s.append(addr_list(e.cc));
    s.push_back(' ');
    s.append(addr_list(e.bcc));
    s.push_back(' ');
    s.append(e.in_reply_to.empty() ? "NIL" : quote_astring(e.in_reply_to));
    s.push_back(' ');
    s.append(e.message_id.empty() ? "NIL" : quote_astring(e.message_id));
    s.push_back(')');
    return s;
}

std::string format_literal_body(std::string_view field_name, std::string_view data) {
    std::string s;
    s.append(field_name);
    s.append(" {");
    s.append(std::to_string(data.size()));
    s.append("}\r\n");
    s.append(data);
    return s;
}

std::string capability_list() {
    // IMAP4rev1 required; AUTH=PLAIN for LOGIN-style clients.
    return "CAPABILITY IMAP4rev1 AUTH=PLAIN";
}

std::string format_list_line(std::string_view name, bool noselect) {
    std::string s = "LIST (";
    if (noselect) {
        s.append("\\Noselect");
    }
    s.append(") \"/\" ");
    s.append(quote_astring(name));
    return s;
}

} // namespace imap
