#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace imap {

// RFC 3501 session states.
enum class session_state {
    not_authenticated,
    authenticated,
    selected,
    logout,
};

enum class command_kind {
    capability,
    noop,
    logout,
    login,
    select,
    examine,
    create,
    delete_mbox,
    rename,
    subscribe,
    unsubscribe,
    list,
    lsub,
    status,
    append,
    check,
    close,
    expunge,
    search,
    fetch,
    store,
    copy,
    uid,          // UID <cmd> ...
    unknown,
    empty,
};

struct atom {
    std::string value;
};

// Parsed client line (after any continuing literals are assembled).
struct client_command {
    std::string tag;
    command_kind kind = command_kind::unknown;
    std::string name;                 // uppercase command verb (or "UID")
    std::vector<std::string> args;    // remaining tokens / strings / literals
    bool uid_mode = false;            // true when command was "UID X"
    command_kind uid_sub = command_kind::unknown;
};

// System / keyword flags (store without leading backslash for system flags name).
struct message_flags {
    bool answered = false;
    bool flagged = false;
    bool deleted = false;
    bool seen = false;
    bool draft = false;
    bool recent = false;
    std::vector<std::string> keywords;

    [[nodiscard]] bool has_system(std::string_view name) const {
        if (name == "\\Answered") {
            return answered;
        }
        if (name == "\\Flagged") {
            return flagged;
        }
        if (name == "\\Deleted") {
            return deleted;
        }
        if (name == "\\Seen") {
            return seen;
        }
        if (name == "\\Draft") {
            return draft;
        }
        if (name == "\\Recent") {
            return recent;
        }
        return false;
    }

    void set_system(std::string_view name, bool on) {
        if (name == "\\Answered") {
            answered = on;
        } else if (name == "\\Flagged") {
            flagged = on;
        } else if (name == "\\Deleted") {
            deleted = on;
        } else if (name == "\\Seen") {
            seen = on;
        } else if (name == "\\Draft") {
            draft = on;
        } else if (name == "\\Recent") {
            recent = on;
        }
    }

    void add_keyword(std::string kw) {
        for (const auto& k : keywords) {
            if (k == kw) {
                return;
            }
        }
        keywords.push_back(std::move(kw));
    }

    void remove_keyword(std::string_view kw) {
        keywords.erase(
            std::remove_if(keywords.begin(), keywords.end(),
                           [&](const std::string& k) { return k == kw; }),
            keywords.end());
    }
};

struct envelope {
    std::string date;
    std::string subject;
    std::string from;     // simplified single address form
    std::string sender;
    std::string reply_to;
    std::string to;
    std::string cc;
    std::string bcc;
    std::string in_reply_to;
    std::string message_id;
};

struct message {
    uint32_t uid = 0;
    message_flags flags;
    std::string internaldate; // IMAP date-time
    envelope env;
    std::string raw; // full RFC822-ish body (headers + body)
};

struct mailbox {
    std::string name;
    uint32_t uidvalidity = 1;
    uint32_t uidnext = 1;
    bool subscribed = true;
    std::vector<message> messages; // sequence = index+1 among non-expunged

    [[nodiscard]] std::size_t exists() const {
        return messages.size();
    }

    [[nodiscard]] std::size_t recent_count() const {
        std::size_t n = 0;
        for (const auto& m : messages) {
            if (m.flags.recent) {
                ++n;
            }
        }
        return n;
    }

    [[nodiscard]] std::size_t unseen_first() const {
        for (std::size_t i = 0; i < messages.size(); ++i) {
            if (!messages[i].flags.seen) {
                return i + 1;
            }
        }
        return 0;
    }
};

inline std::string_view trim_view(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'
                          || s.back() == '\n')) {
        s.remove_suffix(1);
    }
    return s;
}

inline std::string to_upper(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        out.push_back(static_cast<char>(std::toupper(c)));
    }
    return out;
}

inline command_kind parse_command_name(std::string_view name) {
    const auto u = to_upper(name);
    if (u == "CAPABILITY") {
        return command_kind::capability;
    }
    if (u == "NOOP") {
        return command_kind::noop;
    }
    if (u == "LOGOUT") {
        return command_kind::logout;
    }
    if (u == "LOGIN") {
        return command_kind::login;
    }
    if (u == "SELECT") {
        return command_kind::select;
    }
    if (u == "EXAMINE") {
        return command_kind::examine;
    }
    if (u == "CREATE") {
        return command_kind::create;
    }
    if (u == "DELETE") {
        return command_kind::delete_mbox;
    }
    if (u == "RENAME") {
        return command_kind::rename;
    }
    if (u == "SUBSCRIBE") {
        return command_kind::subscribe;
    }
    if (u == "UNSUBSCRIBE") {
        return command_kind::unsubscribe;
    }
    if (u == "LIST") {
        return command_kind::list;
    }
    if (u == "LSUB") {
        return command_kind::lsub;
    }
    if (u == "STATUS") {
        return command_kind::status;
    }
    if (u == "APPEND") {
        return command_kind::append;
    }
    if (u == "CHECK") {
        return command_kind::check;
    }
    if (u == "CLOSE") {
        return command_kind::close;
    }
    if (u == "EXPUNGE") {
        return command_kind::expunge;
    }
    if (u == "SEARCH") {
        return command_kind::search;
    }
    if (u == "FETCH") {
        return command_kind::fetch;
    }
    if (u == "STORE") {
        return command_kind::store;
    }
    if (u == "COPY") {
        return command_kind::copy;
    }
    if (u == "UID") {
        return command_kind::uid;
    }
    return command_kind::unknown;
}

} // namespace imap
