#include "smtp_parser.h"
#include <algorithm>
#include <cctype>

std::string SmtpParser::trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::optional<SmtpRequest> SmtpParser::parse_line(const std::string& line) {
    std::string trimmed = trim(line);
    if (trimmed.empty()) return std::nullopt;

    size_t space_pos = trimmed.find(' ');
    std::string cmd_str = (space_pos == std::string::npos) ? trimmed : trimmed.substr(0, space_pos);
    std::string arg = (space_pos == std::string::npos) ? "" : trimmed.substr(space_pos + 1);

    std::transform(cmd_str.begin(), cmd_str.end(), cmd_str.begin(), ::toupper);

    SmtpCommand cmd = SmtpCommand::UNKNOWN;
    if (cmd_str == "HELO") cmd = SmtpCommand::HELO;
    else if (cmd_str == "EHLO") cmd = SmtpCommand::EHLO;
    else if (cmd_str == "MAIL") cmd = SmtpCommand::MAIL;
    else if (cmd_str == "RCPT") cmd = SmtpCommand::RCPT;
    else if (cmd_str == "DATA") cmd = SmtpCommand::DATA;
    else if (cmd_str == "QUIT") cmd = SmtpCommand::QUIT;
    else if (cmd_str == "RSET") cmd = SmtpCommand::RSET;
    else if (cmd_str == "NOOP") cmd = SmtpCommand::NOOP;

    return SmtpRequest{cmd, arg};
}
