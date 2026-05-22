#pragma once
#include <string>
#include <optional>

enum class SmtpCommand {
    HELO, EHLO, MAIL, RCPT, DATA, QUIT, RSET, NOOP, UNKNOWN
};

struct SmtpRequest {
    SmtpCommand cmd;
    std::string arg;
};

class SmtpParser {
public:
    static std::optional<SmtpRequest> parse_line(const std::string& line);
    static std::string trim(const std::string& s);
};
