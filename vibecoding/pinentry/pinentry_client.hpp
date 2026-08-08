// pinentry_client.hpp
//
// Barebones C++23 client for `pinentry` on Linux.
//
// pinentry is spawned as a child process. Communication happens over two
// pipes wired to the child's stdin/stdout, using GnuPG's Assuan protocol:
// line-based commands out ("SETDESC ...", "GETPIN"), line-based responses
// in ("OK", "ERR ...", "D <data>").
//
// No external dependencies. Only POSIX (fork/exec/pipe) + libstdc++.

#pragma once

#include <cstdio>
#include <expected>
#include <string>
#include <string_view>
#include <sys/types.h>

class PinentryClient {
public:
    // pinentry_path: binary to exec, e.g. "pinentry", "pinentry-curses",
    // "pinentry-gtk-2", "pinentry-qt". Resolved via $PATH.
    static std::expected<PinentryClient, std::string>
    create(std::string_view pinentry_path = "pinentry");

    ~PinentryClient();

    PinentryClient(const PinentryClient&) = delete;
    PinentryClient& operator=(const PinentryClient&) = delete;
    PinentryClient(PinentryClient&& other) noexcept;
    PinentryClient& operator=(PinentryClient&& other) noexcept;

    std::expected<void, std::string> setTitle(std::string_view title);
    std::expected<void, std::string> setDesc(std::string_view desc);
    std::expected<void, std::string> setPrompt(std::string_view prompt);
    std::expected<void, std::string> setOkLabel(std::string_view label);
    std::expected<void, std::string> setCancelLabel(std::string_view label);
    std::expected<void, std::string> setError(std::string_view msg);

    // Asks for a secret (passphrase/PIN). Caller should wipe the returned
    // buffer (e.g. explicit_bzero) once done with it.
    std::expected<std::string, std::string> getPin();

    // Yes/no confirmation dialog using whatever SETDESC/labels were set.
    std::expected<void, std::string> confirm();

private:
    PinentryClient() = default;

    pid_t child_pid_ = -1;
    FILE* to_child_ = nullptr;   // parent write end -> child stdin
    FILE* from_child_ = nullptr; // parent read end  <- child stdout

    void closeAndReap();

    std::expected<void, std::string> setTTY();
    std::expected<std::string, std::string> readResponse();
    std::expected<void, std::string> sendCommand(std::string_view cmd);

    static std::string percentEncode(std::string_view s);
    static std::string percentDecode(std::string_view s);
};
