#include "smtp_session.h"
#include "smtp_parser.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <ctime>
#include <iomanip>
#include <cstring>

namespace fs = std::filesystem;

SmtpSession::SmtpSession(uv_tcp_t* client) : client_(client) {
    client_->data = this;
}

SmtpSession::~SmtpSession() {
    if (client_ && !uv_is_closing((uv_handle_t*)client_)) {
        uv_close((uv_handle_t*)client_, close_cb);
    }
}

void SmtpSession::start() {
    write("220 smtp server Service ready\r\n");
    uv_read_start((uv_stream_t*)client_, alloc_cb, read_cb);
}

void SmtpSession::write(const std::string& data) {
    if (closed_ || data.empty()) return;

    char* buffer = new char[data.size()];
    std::memcpy(buffer, data.data(), data.size());

    auto* buf = new uv_buf_t{ buffer, static_cast<unsigned int>(data.size()) };
    auto* req = new uv_write_t;
    req->data = buf;

    uv_write(req, (uv_stream_t*)client_, buf, 1, write_cb);
}

void SmtpSession::alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    buf->base = new char[suggested_size];
    buf->len = suggested_size;
}

void SmtpSession::read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    auto* session = static_cast<SmtpSession*>(stream->data);
    if (!session) {
        if (buf->base) delete[] buf->base;
        return;
    }

    if (nread < 0) {
        if (buf->base) delete[] buf->base;
        session->closed_ = true;
        uv_close((uv_handle_t*)stream, close_cb);
        return;
    }

    if (nread > 0) {
        session->read_buffer_.append(buf->base, static_cast<size_t>(nread));
    }
    if (buf->base) delete[] buf->base;

    size_t pos;
    while ((pos = session->read_buffer_.find("\r\n")) != std::string::npos) {
        std::string line = session->read_buffer_.substr(0, pos);
        session->read_buffer_.erase(0, pos + 2);
        session->process_line(line);
    }
}

void SmtpSession::write_cb(uv_write_t* req, int status) {
    if (req && req->data) {
        auto* b = static_cast<uv_buf_t*>(req->data);
        if (b->base) delete[] b->base;
        delete b;
    }
    delete req;
    if (status < 0) {
        std::cerr << "Write failed: " << uv_strerror(status) << "\n";
    }
}

void SmtpSession::close_cb(uv_handle_t* handle) {
    delete static_cast<SmtpSession*>(handle->data);
}

void SmtpSession::process_line(const std::string& line) {
    if (in_data_mode_) {
        if (line == ".") {
            in_data_mode_ = false;
            save_email();
            write("250 OK queued\r\n");
            data_buffer_.clear();
        } else {
            data_buffer_ += line + "\r\n";
        }
        return;
    }

    auto req_opt = SmtpParser::parse_line(line);
    if (req_opt) {
        handle_command(*req_opt);
    } else {
        write("500 Syntax error\r\n");
    }
}

std::string SmtpSession::extract_email_address(const std::string& arg) {
    size_t start = arg.find('<');
    if (start != std::string::npos) {
        size_t end = arg.find('>', start);
        if (end != std::string::npos) {
            return arg.substr(start + 1, end - start - 1);
        }
    }
    return arg;
}

void SmtpSession::save_email() {
    fs::create_directories("mailbox");

    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);

    std::ostringstream filename;
    filename << "mailbox/email_"
             << std::put_time(&tm, "%Y%m%d_%H%M%S") << "_"
             << std::hash<std::string>{}(current_mail_from_) % 10000
             << ".eml";

    std::ofstream out(filename.str(), std::ios::binary);
    if (!out) {
        std::cerr << "[SMTP] Failed to save email to " << filename.str() << "\n";
        return;
    }

    std::string clean_from = extract_email_address(current_mail_from_);
    std::string clean_to = rcpt_to_.empty() ? "" : extract_email_address(rcpt_to_[0]);

    // Write proper headers
    out << "From: " << clean_from << "\r\n";
    if (!clean_to.empty()) {
        out << "To: " << clean_to << "\r\n";
    }
    out << "Date: " << std::put_time(&tm, "%a, %d %b %Y %H:%M:%S %z") << "\r\n";
    out << "Message-ID: <" << std::time(nullptr) << "." 
        << std::hash<std::string>{}(current_mail_from_) % 100000 
        << "@smtp_server>\r\n";

    // Add the raw data sent by client (which already contains Subject and body)
    out << data_buffer_;

    std::cout << "[SMTP] Saved: " << filename.str() << "\n";
}

void SmtpSession::handle_command(const SmtpRequest& req) {
    switch (req.cmd) {
        case SmtpCommand::HELO:
        case SmtpCommand::EHLO:
            write("250 OK\r\n");
            break;

        case SmtpCommand::MAIL:
            current_mail_from_ = req.arg;
            rcpt_to_.clear();
            data_buffer_.clear();
            write("250 OK\r\n");
            break;

        case SmtpCommand::RCPT:
            rcpt_to_.push_back(req.arg);
            write("250 OK\r\n");
            break;

        case SmtpCommand::DATA:
            if (rcpt_to_.empty()) {
                write("503 Bad sequence of commands\r\n");
            } else {
                in_data_mode_ = true;
                write("354 Start mail input; end with <CRLF>.<CRLF>\r\n");
            }
            break;

        case SmtpCommand::QUIT:
            write("221 Service closing\r\n");
            closed_ = true;
            uv_close((uv_handle_t*)client_, close_cb);
            break;

        case SmtpCommand::RSET:
            current_mail_from_.clear();
            rcpt_to_.clear();
            data_buffer_.clear();
            in_data_mode_ = false;
            write("250 OK\r\n");
            break;

        case SmtpCommand::NOOP:
            write("250 OK\r\n");
            break;

        default:
            write("502 Command not implemented\r\n");
    }
}
