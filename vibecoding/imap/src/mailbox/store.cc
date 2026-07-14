#include "mailbox/store.hh"

#include <algorithm>
#include <cctype>

namespace imap {
namespace {

std::string normalize_mbox(std::string_view name) {
    // Barebones: case-sensitive names; treat INBOX specially (case-insensitive).
    if (name.size() == 5) {
        auto u = to_upper(name);
        if (u == "INBOX") {
            return "INBOX";
        }
    }
    return std::string(name);
}

std::string header_value(std::string_view raw, std::string_view key) {
    // key without colon, case-insensitive match at line start.
    std::string prefix(key);
    prefix.push_back(':');
    auto upper_prefix = to_upper(prefix);

    std::size_t pos = 0;
    while (pos < raw.size()) {
        auto eol = raw.find("\r\n", pos);
        if (eol == std::string_view::npos) {
            eol = raw.size();
        }
        auto line = raw.substr(pos, eol - pos);
        if (line.empty()) {
            break; // end of headers
        }
        if (line.size() >= prefix.size()) {
            auto head = to_upper(line.substr(0, prefix.size()));
            if (head == upper_prefix) {
                auto val = line.substr(prefix.size());
                return std::string(trim_view(val));
            }
        }
        if (eol == raw.size()) {
            break;
        }
        pos = eol + 2;
    }
    return {};
}

std::string extract_email(std::string_view from_field) {
    auto lt = from_field.rfind('<');
    auto gt = from_field.rfind('>');
    if (lt != std::string_view::npos && gt != std::string_view::npos && gt > lt) {
        return std::string(from_field.substr(lt + 1, gt - lt - 1));
    }
    return std::string(trim_view(from_field));
}

} // namespace

envelope parse_headers_to_envelope(std::string_view raw) {
    envelope e;
    e.date = header_value(raw, "Date");
    e.subject = header_value(raw, "Subject");
    e.from = extract_email(header_value(raw, "From"));
    e.to = extract_email(header_value(raw, "To"));
    e.cc = extract_email(header_value(raw, "Cc"));
    e.message_id = header_value(raw, "Message-ID");
    e.in_reply_to = header_value(raw, "In-Reply-To");
    e.sender = e.from;
    e.reply_to = extract_email(header_value(raw, "Reply-To"));
    if (e.reply_to.empty()) {
        e.reply_to = e.from;
    }
    return e;
}

bool mailbox_name_matches(std::string_view pattern, std::string_view name) {
    // Simplified: * matches any, % matches within one hierarchy level (/).
    std::size_t pi = 0, ni = 0;
    std::size_t star_p = std::string_view::npos, star_n = 0;
    while (ni < name.size()) {
        if (pi < pattern.size() && (pattern[pi] == name[ni] || pattern[pi] == '%')) {
            if (pattern[pi] == '%' && name[ni] == '/') {
                // % cannot cross /
                if (star_p != std::string_view::npos) {
                    pi = star_p + 1;
                    ni = ++star_n;
                    continue;
                }
                return false;
            }
            ++pi;
            ++ni;
        } else if (pi < pattern.size() && pattern[pi] == '*') {
            star_p = pi++;
            star_n = ni;
        } else if (star_p != std::string_view::npos) {
            pi = star_p + 1;
            ni = ++star_n;
        } else {
            return false;
        }
    }
    while (pi < pattern.size() && (pattern[pi] == '*' || pattern[pi] == '%')) {
        ++pi;
    }
    return pi == pattern.size();
}

message user_store::make_sample(uint32_t uid,
                                std::string_view subject,
                                std::string_view from,
                                std::string_view body,
                                bool seen) {
    message m;
    m.uid = uid;
    m.flags.seen = seen;
    m.flags.recent = !seen;
    m.internaldate = "13-Jul-2026 12:00:00 +0000";
    m.env.date = "Mon, 13 Jul 2026 12:00:00 +0000";
    m.env.subject = std::string(subject);
    m.env.from = std::string(from);
    m.env.sender = m.env.from;
    m.env.reply_to = m.env.from;
    m.env.to = "demo@localhost";
    m.env.message_id = "<msg" + std::to_string(uid) + "@localhost>";

    std::string raw;
    raw.append("From: ");
    raw.append(from);
    raw.append("\r\nTo: demo@localhost\r\nSubject: ");
    raw.append(subject);
    raw.append("\r\nDate: Mon, 13 Jul 2026 12:00:00 +0000\r\nMessage-ID: ");
    raw.append(m.env.message_id);
    raw.append("\r\nMIME-Version: 1.0\r\nContent-Type: text/plain; charset=utf-8\r\n\r\n");
    raw.append(body);
    if (!body.empty() && body.back() != '\n') {
        raw.append("\r\n");
    }
    m.raw = std::move(raw);
    return m;
}

user_store::user_store(std::string username) : _user(std::move(username)) {
    seed_demo();
}

void user_store::seed_demo() {
    mailbox inbox;
    inbox.name = "INBOX";
    inbox.uidvalidity = 1;
    inbox.uidnext = 4;
    inbox.messages.push_back(
        make_sample(1, "Welcome to barebones IMAP", "server@localhost",
                    "Hello from the Seastar IMAP demo server.\r\n", true));
    inbox.messages.push_back(
        make_sample(2, "Second message", "alice@example.com",
                    "This is message number two.\r\n", false));
    inbox.messages.push_back(
        make_sample(3, "Flagged idea", "bob@example.com",
                    "Remember to implement IDLE someday.\r\n", false));
    inbox.messages.back().flags.flagged = true;

    mailbox sent;
    sent.name = "Sent";
    sent.uidvalidity = 1;
    sent.uidnext = 1;

    mailbox drafts;
    drafts.name = "Drafts";
    drafts.uidvalidity = 1;
    drafts.uidnext = 1;

    _mailboxes.emplace(inbox.name, std::move(inbox));
    _mailboxes.emplace(sent.name, std::move(sent));
    _mailboxes.emplace(drafts.name, std::move(drafts));
}

mailbox* user_store::find(std::string_view name) {
    auto key = normalize_mbox(name);
    auto it = _mailboxes.find(key);
    if (it == _mailboxes.end()) {
        return nullptr;
    }
    return &it->second;
}

const mailbox* user_store::find(std::string_view name) const {
    auto key = normalize_mbox(name);
    auto it = _mailboxes.find(key);
    if (it == _mailboxes.end()) {
        return nullptr;
    }
    return &it->second;
}

std::vector<std::string> user_store::list_names(std::string_view reference,
                                                std::string_view mailbox_glob) const {
    // RFC 3501: reference + mailbox; barebones concatenates if mailbox not absolute.
    std::string pattern;
    if (!mailbox_glob.empty() && mailbox_glob.front() == '/') {
        pattern = std::string(mailbox_glob.substr(1));
    } else {
        pattern = std::string(reference);
        if (!pattern.empty() && pattern.back() != '/' && !mailbox_glob.empty()) {
            // no hierarchy join for flat names
        }
        pattern.append(mailbox_glob);
    }
    if (pattern.empty()) {
        pattern = "*";
    }

    std::vector<std::string> names;
    for (const auto& [n, m] : _mailboxes) {
        (void)m;
        if (mailbox_name_matches(pattern, n)) {
            names.push_back(n);
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool user_store::create(std::string_view name) {
    auto key = normalize_mbox(name);
    if (key.empty() || _mailboxes.count(key)) {
        return false;
    }
    mailbox m;
    m.name = key;
    m.uidvalidity = 1;
    m.uidnext = 1;
    _mailboxes.emplace(key, std::move(m));
    return true;
}

bool user_store::remove(std::string_view name) {
    auto key = normalize_mbox(name);
    if (key == "INBOX") {
        return false;
    }
    return _mailboxes.erase(key) > 0;
}

bool user_store::rename(std::string_view from, std::string_view to) {
    auto fk = normalize_mbox(from);
    auto tk = normalize_mbox(to);
    if (fk == "INBOX" || tk.empty() || _mailboxes.count(tk) || !_mailboxes.count(fk)) {
        return false;
    }
    auto node = _mailboxes.extract(fk);
    node.key() = tk;
    node.mapped().name = tk;
    _mailboxes.insert(std::move(node));
    return true;
}

bool user_store::subscribe(std::string_view name, bool on) {
    auto* m = find(name);
    if (!m) {
        return false;
    }
    m->subscribed = on;
    return true;
}

std::optional<uint32_t> user_store::append(std::string_view mbox_name,
                                           const message_flags& flags,
                                           std::string_view internaldate,
                                           std::string raw) {
    auto* mbox = find(mbox_name);
    if (!mbox) {
        return std::nullopt;
    }
    message msg;
    msg.uid = mbox->uidnext++;
    msg.flags = flags;
    msg.flags.recent = true;
    msg.internaldate = internaldate.empty() ? "13-Jul-2026 12:00:00 +0000"
                                            : std::string(internaldate);
    msg.env = parse_headers_to_envelope(raw);
    msg.raw = std::move(raw);
    mbox->messages.push_back(std::move(msg));
    return mbox->messages.back().uid;
}

bool user_store::copy_messages(mailbox& src,
                               const std::vector<uint32_t>& seqs,
                               std::string_view dest) {
    auto* d = find(dest);
    if (!d) {
        return false;
    }
    for (uint32_t seq : seqs) {
        if (seq == 0 || seq > src.messages.size()) {
            continue;
        }
        message copy = src.messages[seq - 1];
        copy.uid = d->uidnext++;
        copy.flags.recent = true;
        d->messages.push_back(std::move(copy));
    }
    return true;
}

std::vector<uint32_t> user_store::expunge(mailbox& mbox) {
    std::vector<uint32_t> expunged;
    // Collect sequences to remove (1-based), ascending first.
    for (std::size_t i = 0; i < mbox.messages.size(); ++i) {
        if (mbox.messages[i].flags.deleted) {
            expunged.push_back(static_cast<uint32_t>(i + 1));
        }
    }
    // Erase from back so indices stay valid.
    for (auto it = expunged.rbegin(); it != expunged.rend(); ++it) {
        mbox.messages.erase(mbox.messages.begin() + static_cast<std::ptrdiff_t>(*it - 1));
    }
    // Wire protocol expects ascending EXPUNGE with renumbering: send original ascending
    // sequences; clients renumber. Return ascending.
    return expunged;
}

} // namespace imap
