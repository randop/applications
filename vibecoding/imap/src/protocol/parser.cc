#include "protocol/parser.hh"

#include <cctype>
#include <charconv>
#include <set>

namespace imap {
namespace {

bool is_atom_char(char c) {
    // RFC 3501 atom-specials excluded roughly.
    if (c <= 0x1f || c == 0x7f) {
        return false;
    }
    switch (c) {
    case '(':
    case ')':
    case '{':
    case ' ':
    case '%':
    case '*':
    case '"':
    case '\\':
    case ']':
        return false;
    default:
        return true;
    }
}

// Parse next token from `p`, advance `p`. Returns false on syntax error.
bool next_token(std::string_view& p, std::string& out, std::string& err) {
    while (!p.empty() && p.front() == ' ') {
        p.remove_prefix(1);
    }
    if (p.empty()) {
        err = "unexpected end of command";
        return false;
    }
    if (p.front() == '"') {
        p.remove_prefix(1);
        std::string s;
        while (!p.empty()) {
            char c = p.front();
            p.remove_prefix(1);
            if (c == '\\') {
                if (p.empty()) {
                    err = "unterminated quoted string escape";
                    return false;
                }
                s.push_back(p.front());
                p.remove_prefix(1);
                continue;
            }
            if (c == '"') {
                out = std::move(s);
                return true;
            }
            s.push_back(c);
        }
        err = "unterminated quoted string";
        return false;
    }
    if (p.front() == '(') {
        // Keep balanced paren list as one token including outer parens.
        int depth = 0;
        std::size_t i = 0;
        for (; i < p.size(); ++i) {
            if (p[i] == '(') {
                ++depth;
            } else if (p[i] == ')') {
                --depth;
                if (depth == 0) {
                    out = std::string(p.substr(0, i + 1));
                    p.remove_prefix(i + 1);
                    return true;
                }
            } else if (p[i] == '"') {
                ++i;
                while (i < p.size()) {
                    if (p[i] == '\\' && i + 1 < p.size()) {
                        i += 2;
                        continue;
                    }
                    if (p[i] == '"') {
                        break;
                    }
                    ++i;
                }
            }
        }
        err = "unbalanced parenthesis";
        return false;
    }
    // Atom / number / sequence-ish
    std::size_t i = 0;
    while (i < p.size() && p[i] != ' ' && p[i] != '(' && p[i] != ')') {
        // Allow sequence set chars in atoms for simplicity (* : , digits etc.)
        if (p[i] == '"') {
            break;
        }
        ++i;
    }
    if (i == 0) {
        err = "empty token";
        return false;
    }
    out = std::string(p.substr(0, i));
    p.remove_prefix(i);
    return true;
}

std::optional<std::size_t> parse_literal_size(std::string_view line, bool* non_sync) {
    // Look for trailing {n} or {n+} before optional spaces (line without CRLF).
    auto s = trim_view(line);
    if (s.empty() || s.back() != '}') {
        return std::nullopt;
    }
    auto open = s.rfind('{');
    if (open == std::string_view::npos) {
        return std::nullopt;
    }
    // Ensure this is the end of the line token (optional spaces after })
    // and nothing significant after }
    std::string_view after = s.substr(s.find('}') + 1);
    after = trim_view(after);
    if (!after.empty()) {
        return std::nullopt;
    }
    std::string_view num = s.substr(open + 1, s.size() - open - 2);
    *non_sync = false;
    if (!num.empty() && num.back() == '+') {
        *non_sync = true;
        num.remove_suffix(1);
    }
    if (num.empty()) {
        return std::nullopt;
    }
    std::size_t n = 0;
    auto [ptr, ec] = std::from_chars(num.data(), num.data() + num.size(), n);
    if (ec != std::errc{} || ptr != num.data() + num.size()) {
        return std::nullopt;
    }
    return n;
}

// Replace trailing `{n}` / `{n+}` with a single space so tokens parse; literal content
// is appended by caller as a quoted string or raw arg via special marker.
std::string strip_trailing_literal_marker(std::string_view line) {
    auto s = trim_view(line);
    auto open = s.rfind('{');
    if (open == std::string_view::npos) {
        return std::string(s);
    }
    return std::string(trim_view(s.substr(0, open)));
}

} // namespace

line_scan scan_command_line(std::string_view line) {
    line_scan out;
    auto trimmed = trim_view(line);
    if (trimmed.empty()) {
        out.st = line_scan::status::complete;
        out.cmd.kind = command_kind::empty;
        return out;
    }

    bool non_sync = false;
    auto lit = parse_literal_size(trimmed, &non_sync);
    if (lit) {
        out.st = line_scan::status::need_literal;
        out.literal_octets = *lit;
        // Partial text stored by caller; we only signal need.
        return out;
    }

    auto pr = parse_assembled_command(trimmed);
    if (!pr.ok) {
        out.st = line_scan::status::syntax_error;
        out.error = std::move(pr.error);
        return out;
    }
    out.st = line_scan::status::complete;
    out.cmd = std::move(pr.cmd);
    return out;
}

parse_result parse_assembled_command(std::string_view text) {
    parse_result r;
    auto p = trim_view(text);
    if (p.empty()) {
        r.ok = true;
        r.cmd.kind = command_kind::empty;
        return r;
    }

    std::string tag;
    std::string err;
    if (!next_token(p, tag, err)) {
        r.error = err;
        return r;
    }
    if (tag == "+" || tag.empty()) {
        r.error = "invalid tag";
        return r;
    }
    r.cmd.tag = std::move(tag);

    while (!p.empty() && p.front() == ' ') {
        p.remove_prefix(1);
    }
    if (p.empty()) {
        r.error = "missing command";
        return r;
    }

    std::string name;
    if (!next_token(p, name, err)) {
        r.error = err;
        return r;
    }
    r.cmd.name = to_upper(name);
    r.cmd.kind = parse_command_name(r.cmd.name);

    if (r.cmd.kind == command_kind::uid) {
        r.cmd.uid_mode = true;
        while (!p.empty() && p.front() == ' ') {
            p.remove_prefix(1);
        }
        if (p.empty()) {
            r.error = "UID requires a subcommand";
            return r;
        }
        std::string sub;
        if (!next_token(p, sub, err)) {
            r.error = err;
            return r;
        }
        r.cmd.name = to_upper(sub);
        r.cmd.uid_sub = parse_command_name(r.cmd.name);
        r.cmd.kind = r.cmd.uid_sub;
    }

    while (true) {
        while (!p.empty() && p.front() == ' ') {
            p.remove_prefix(1);
        }
        if (p.empty()) {
            break;
        }
        std::string tok;
        if (!next_token(p, tok, err)) {
            r.error = err;
            return r;
        }
        r.cmd.args.push_back(std::move(tok));
    }

    r.ok = true;
    return r;
}

void append_literal_to_buffer(std::string& acc, std::string_view literal_bytes) {
    // Encode literal as a quoted string so the tokenizer accepts it as one arg.
    acc.push_back(' ');
    acc.push_back('"');
    for (char c : literal_bytes) {
        if (c == '\\' || c == '"') {
            acc.push_back('\\');
        }
        // IMAP quoted-string is 7-bit-ish; pass through for barebones.
        acc.push_back(c);
    }
    acc.push_back('"');
}

std::vector<uint32_t> parse_sequence_set(std::string_view set, uint32_t max_seq, bool* ok) {
    std::set<uint32_t> ids;
    *ok = true;
    if (max_seq == 0) {
        // Empty mailbox: only invalid if set is non-empty specials — treat as empty set.
        return {};
    }

    auto parse_num = [&](std::string_view n) -> uint32_t {
        if (n == "*") {
            return max_seq;
        }
        uint32_t v = 0;
        auto [ptr, ec] = std::from_chars(n.data(), n.data() + n.size(), v);
        if (ec != std::errc{} || ptr != n.data() + n.size() || v == 0) {
            *ok = false;
            return 0;
        }
        return v;
    };

    std::size_t start = 0;
    while (start <= set.size()) {
        auto comma = set.find(',', start);
        auto part = set.substr(start, comma == std::string_view::npos ? set.size() - start
                                                                      : comma - start);
        if (part.empty()) {
            *ok = false;
            return {};
        }
        auto colon = part.find(':');
        if (colon == std::string_view::npos) {
            uint32_t v = parse_num(part);
            if (!*ok) {
                return {};
            }
            if (v >= 1 && v <= max_seq) {
                ids.insert(v);
            }
        } else {
            uint32_t a = parse_num(part.substr(0, colon));
            uint32_t b = parse_num(part.substr(colon + 1));
            if (!*ok) {
                return {};
            }
            if (a > b) {
                std::swap(a, b);
            }
            a = std::max(a, 1u);
            b = std::min(b, max_seq);
            for (uint32_t i = a; i <= b; ++i) {
                ids.insert(i);
            }
        }
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    return std::vector<uint32_t>(ids.begin(), ids.end());
}

std::vector<uint32_t> parse_uid_set_to_seq(std::string_view set,
                                           const std::vector<message>& messages,
                                           bool* ok) {
    *ok = true;
    if (messages.empty()) {
        return {};
    }
    uint32_t max_uid = 0;
    for (const auto& m : messages) {
        max_uid = std::max(max_uid, m.uid);
    }

    auto parse_num = [&](std::string_view n) -> uint32_t {
        if (n == "*") {
            return max_uid;
        }
        uint32_t v = 0;
        auto [ptr, ec] = std::from_chars(n.data(), n.data() + n.size(), v);
        if (ec != std::errc{} || ptr != n.data() + n.size() || v == 0) {
            *ok = false;
            return 0;
        }
        return v;
    };

    std::set<uint32_t> uids;
    std::size_t start = 0;
    while (start <= set.size()) {
        auto comma = set.find(',', start);
        auto part = set.substr(start, comma == std::string_view::npos ? set.size() - start
                                                                      : comma - start);
        if (part.empty()) {
            *ok = false;
            return {};
        }
        auto colon = part.find(':');
        if (colon == std::string_view::npos) {
            uids.insert(parse_num(part));
            if (!*ok) {
                return {};
            }
        } else {
            uint32_t a = parse_num(part.substr(0, colon));
            uint32_t b = parse_num(part.substr(colon + 1));
            if (!*ok) {
                return {};
            }
            if (a > b) {
                std::swap(a, b);
            }
            for (uint32_t u = a; u <= b; ++u) {
                uids.insert(u);
            }
        }
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }

    std::vector<uint32_t> seqs;
    for (std::size_t i = 0; i < messages.size(); ++i) {
        if (uids.count(messages[i].uid)) {
            seqs.push_back(static_cast<uint32_t>(i + 1));
        }
    }
    return seqs;
}

std::vector<std::string> split_paren_list(std::string_view list) {
    std::vector<std::string> items;
    auto s = trim_view(list);
    if (s.size() >= 2 && s.front() == '(' && s.back() == ')') {
        s.remove_prefix(1);
        s.remove_suffix(1);
    }
    s = trim_view(s);
    if (s.empty()) {
        return items;
    }
    // Macro names or nested — barebones: space-separated tokens, no deep nesting.
    std::string_view p = s;
    std::string err;
    while (!p.empty()) {
        std::string tok;
        if (!next_token(p, tok, err)) {
            break;
        }
        items.push_back(to_upper(tok));
        while (!p.empty() && p.front() == ' ') {
            p.remove_prefix(1);
        }
    }
    return items;
}

std::string unquote_astring(std::string_view token) {
    if (token.size() >= 2 && token.front() == '"' && token.back() == '"') {
        std::string s;
        for (std::size_t i = 1; i + 1 < token.size(); ++i) {
            if (token[i] == '\\' && i + 1 + 1 <= token.size()) {
                s.push_back(token[i + 1]);
                ++i;
            } else {
                s.push_back(token[i]);
            }
        }
        return s;
    }
    return std::string(token);
}

} // namespace imap
