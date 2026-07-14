#pragma once

#include "protocol/types.hh"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace imap {

// Shard-local in-memory mail store for one authenticated user namespace.
class user_store {
public:
    explicit user_store(std::string username);

    [[nodiscard]] const std::string& username() const { return _user; }

    [[nodiscard]] mailbox* find(std::string_view name);
    [[nodiscard]] const mailbox* find(std::string_view name) const;

    [[nodiscard]] std::vector<std::string> list_names(std::string_view reference,
                                                      std::string_view mailbox_glob) const;

    bool create(std::string_view name);
    bool remove(std::string_view name);
    bool rename(std::string_view from, std::string_view to);
    bool subscribe(std::string_view name, bool on);

    // APPEND returns new UID or nullopt on failure.
    std::optional<uint32_t> append(std::string_view mbox_name,
                                   const message_flags& flags,
                                   std::string_view internaldate,
                                   std::string raw);

    // COPY messages by 1-based sequence numbers into destination.
    bool copy_messages(mailbox& src, const std::vector<uint32_t>& seqs, std::string_view dest);

    // EXPUNGE deleted; returns sequence numbers expunged (descending order for wire).
    std::vector<uint32_t> expunge(mailbox& mbox);

    static message make_sample(uint32_t uid,
                               std::string_view subject,
                               std::string_view from,
                               std::string_view body,
                               bool seen);

private:
    void seed_demo();

    std::string _user;
    std::unordered_map<std::string, mailbox> _mailboxes;
};

// Very small IMAP mailbox pattern matcher for LIST (supports % and * roughly).
bool mailbox_name_matches(std::string_view pattern, std::string_view name);

// Parse Date: / Subject: / From: / To: / Message-ID: from raw headers.
envelope parse_headers_to_envelope(std::string_view raw);

} // namespace imap
