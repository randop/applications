#pragma once

#ifndef APP_H
#define APP_H

#include <boost/json/src.hpp>
#include <boost/multiprecision/cpp_dec_float.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <iostream>
#include <algorithm>
#include <boost/program_options.hpp>
#include <chrono>
#include <fmt/base.h>
#include <fmt/ranges.h>
#include <iostream>
#include <iterator>
#include <string>
#include <map>
#include <spdlog/fmt/ranges.h>
#include <spdlog/spdlog.h>
#include <thread>

using namespace boost::asio;
using namespace boost::beast;
using namespace boost::json;
namespace ssl = boost::asio::ssl;
namespace beast = boost::beast;
using boost::asio::ip::tcp;

// Define high-precision decimal type
using namespace boost::multiprecision;
using BigFloat = cpp_dec_float_50;

// Custom tag_invoke for BigFloat
namespace boost::json {

BigFloat tag_invoke(value_to_tag<BigFloat>, const value &jv) {
  if (jv.is_number()) {
    // Use serialize() to convert the number to a string
    std::string num_str = boost::json::serialize(jv);
    return BigFloat(num_str);
  } else if (jv.is_string()) {
    // Directly use the string value
    return BigFloat(jv.as_string().c_str());
  } else {
    throw std::invalid_argument(
        "Expected a number or string for BigFloat conversion");
  }
}

} // namespace boost::json

// Custom formatter for BigFloat
template <> struct fmt::formatter<BigFloat> {
  constexpr auto parse(fmt::format_parse_context &ctx) { return ctx.begin(); }

  template <typename FormatContext>
  auto format(const BigFloat &value, FormatContext &ctx) const {
    return fmt::format_to(ctx.out(), "{:.4f}", value.convert_to<double>());
  }
};

// Struct to hold parsed URL components
struct Url {
  std::string protocol; // http or https
  std::string host;
  std::string port;
  std::string target;

  static Url parse_url(const std::string &url) {
    Url result;
    size_t pos = url.find("://");
    if (pos == std::string::npos) {
      throw std::runtime_error("Invalid URL: no protocol specified");
    }
    result.protocol = url.substr(0, pos);
    if (result.protocol != "http" && result.protocol != "https") {
      throw std::runtime_error("Unsupported protocol: " + result.protocol);
    }

    size_t host_start = pos + 3;
    size_t path_start = url.find('/', host_start);
    if (path_start == std::string::npos) {
      result.host = url.substr(host_start);
      result.target = "/";
    } else {
      result.host = url.substr(host_start, path_start - host_start);
      result.target = url.substr(path_start);
    }

    size_t port_delim = result.host.find(':');
    if (port_delim != std::string::npos) {
      result.port = result.host.substr(port_delim + 1);
      result.host = result.host.substr(0, port_delim);
    } else {
      result.port = (result.protocol == "https") ? "443" : "80";
    }

    return result;
  }
};

// Currency exchange rate defaults
std::map<std::string, BigFloat> exchange_rates = {
    {"USD", 1.0}, {"EUR", 0.91}, {"PHP", 55.84}};

std::string buffers_to_string(beast::multi_buffer::const_buffers_type buffers);

void fetch_exchange_rates();

#endif
