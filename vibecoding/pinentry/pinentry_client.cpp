// pinentry_client.cpp
#include "pinentry_client.hpp"

#include <cstdlib>
#include <cstring>
#include <format>
#include <sys/wait.h>
#include <unistd.h>

// ---------------------------------------------------------------- helpers --

std::string PinentryClient::percentEncode(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        switch (c) {
            case '%':  out += "%25"; break;
            case '\r': out += "%0D"; break;
            case '\n': out += "%0A"; break;
            default:   out += static_cast<char>(c);
        }
    }
    return out;
}

std::string PinentryClient::percentDecode(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            auto hex = std::string(s.substr(i + 1, 2));
            out += static_cast<char>(std::strtoul(hex.c_str(), nullptr, 16));
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

// --------------------------------------------------------------- lifetime --

std::expected<PinentryClient, std::string>
PinentryClient::create(std::string_view pinentry_path) {
    int in_pipe[2];  // parent writes -> child stdin
    int out_pipe[2]; // child stdout -> parent reads

    if (pipe(in_pipe) != 0)
        return std::unexpected(std::format("pipe() failed: {}", std::strerror(errno)));
    if (pipe(out_pipe) != 0) {
        close(in_pipe[0]); close(in_pipe[1]);
        return std::unexpected(std::format("pipe() failed: {}", std::strerror(errno)));
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        return std::unexpected(std::format("fork() failed: {}", std::strerror(errno)));
    }

    if (pid == 0) {
        // --- child ---
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);

        std::string bin(pinentry_path);
        execlp(bin.c_str(), bin.c_str(), nullptr);
        _exit(127); // exec failed
    }

    // --- parent ---
    close(in_pipe[0]);
    close(out_pipe[1]);

    PinentryClient client;
    client.child_pid_ = pid;
    client.to_child_ = fdopen(in_pipe[1], "w");
    client.from_child_ = fdopen(out_pipe[0], "r");

    if (!client.to_child_ || !client.from_child_) {
        return std::unexpected("fdopen() failed on pinentry pipes");
    }

    // pinentry greets with "OK Pleased to meet you" on connect.
    auto banner = client.readResponse();
    if (!banner) {
        return std::unexpected(std::format("pinentry did not start cleanly: {}", banner.error()));
    }

    // Best-effort: tell pinentry which TTY it's attached to. Required by
    // pinentry-curses/pinentry-tty; harmless no-op for GUI variants.
    client.setTTY(); // ignore failure, not fatal for GUI backends

    return client;
}

PinentryClient::PinentryClient(PinentryClient&& other) noexcept
    : child_pid_(other.child_pid_),
      to_child_(other.to_child_),
      from_child_(other.from_child_) {
    other.child_pid_ = -1;
    other.to_child_ = nullptr;
    other.from_child_ = nullptr;
}

PinentryClient& PinentryClient::operator=(PinentryClient&& other) noexcept {
    if (this != &other) {
        closeAndReap();
        child_pid_ = other.child_pid_;
        to_child_ = other.to_child_;
        from_child_ = other.from_child_;
        other.child_pid_ = -1;
        other.to_child_ = nullptr;
        other.from_child_ = nullptr;
    }
    return *this;
}

void PinentryClient::closeAndReap() {
    if (to_child_) {
        std::fputs("BYE\n", to_child_);
        std::fflush(to_child_);
        std::fclose(to_child_);
        to_child_ = nullptr;
    }
    if (from_child_) {
        std::fclose(from_child_);
        from_child_ = nullptr;
    }
    if (child_pid_ > 0) {
        int status = 0;
        waitpid(child_pid_, &status, 0);
        child_pid_ = -1;
    }
}

PinentryClient::~PinentryClient() {
    closeAndReap();
}

// ---------------------------------------------------------------- protocol --

std::expected<std::string, std::string> PinentryClient::readResponse() {
    std::string data;
    char buf[4096];

    while (std::fgets(buf, sizeof(buf), from_child_)) {
        std::string_view line(buf);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
            line.remove_suffix(1);

        if (line.starts_with("OK")) {
            return data;
        } else if (line.starts_with("ERR")) {
            return std::unexpected(std::string(line));
        } else if (line.starts_with("D ")) {
            data += percentDecode(line.substr(2));
        }
        // "S ..." (status), "#..." (comment), "INQUIRE ..." are ignored
        // by this barebones client.
    }
    return std::unexpected("pinentry: unexpected EOF (process exited or crashed)");
}

std::expected<void, std::string> PinentryClient::sendCommand(std::string_view cmd) {
    if (std::fprintf(to_child_, "%.*s\n", static_cast<int>(cmd.size()), cmd.data()) < 0 ||
        std::fflush(to_child_) != 0) {
        return std::unexpected("failed to write to pinentry");
    }
    auto resp = readResponse();
    if (!resp) return std::unexpected(resp.error());
    return {};
}

std::expected<void, std::string> PinentryClient::setTTY() {
    if (const char* tty = ttyname(STDIN_FILENO)) {
        if (auto r = sendCommand(std::format("OPTION ttyname={}", tty)); !r)
            return r;
    }
    if (const char* term = std::getenv("TERM")) {
        if (auto r = sendCommand(std::format("OPTION ttytype={}", term)); !r)
            return r;
    }
    return {};
}

std::expected<void, std::string> PinentryClient::setTitle(std::string_view title) {
    return sendCommand(std::format("SETTITLE {}", percentEncode(title)));
}

std::expected<void, std::string> PinentryClient::setDesc(std::string_view desc) {
    return sendCommand(std::format("SETDESC {}", percentEncode(desc)));
}

std::expected<void, std::string> PinentryClient::setPrompt(std::string_view prompt) {
    return sendCommand(std::format("SETPROMPT {}", percentEncode(prompt)));
}

std::expected<void, std::string> PinentryClient::setOkLabel(std::string_view label) {
    return sendCommand(std::format("SETOK {}", percentEncode(label)));
}

std::expected<void, std::string> PinentryClient::setCancelLabel(std::string_view label) {
    return sendCommand(std::format("SETCANCEL {}", percentEncode(label)));
}

std::expected<void, std::string> PinentryClient::setError(std::string_view msg) {
    return sendCommand(std::format("SETERROR {}", percentEncode(msg)));
}

std::expected<std::string, std::string> PinentryClient::getPin() {
    if (std::fputs("GETPIN\n", to_child_) < 0 || std::fflush(to_child_) != 0)
        return std::unexpected("failed to write to pinentry");
    return readResponse();
}

std::expected<void, std::string> PinentryClient::confirm() {
    return sendCommand("CONFIRM");
}
