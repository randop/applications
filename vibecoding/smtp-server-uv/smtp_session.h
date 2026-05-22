#pragma once
#include <uv.h>
#include <string>
#include <vector>

#include "smtp_parser.h"

class SmtpSession {
public:
    explicit SmtpSession(uv_tcp_t* client);
    ~SmtpSession();

    void start();
    void write(const std::string& data);

private:
    static void alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
    static void read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
    static void write_cb(uv_write_t* req, int status);
    static void close_cb(uv_handle_t* handle);

    void process_line(const std::string& line);
    void handle_command(const SmtpRequest& req);
    void save_email();
    std::string extract_email_address(const std::string& arg);

    uv_tcp_t* client_;
    std::string read_buffer_;
    std::string current_mail_from_;
    std::vector<std::string> rcpt_to_;
    std::string data_buffer_;
    bool in_data_mode_ = false;
    bool closed_ = false;
};
