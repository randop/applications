#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include <algorithm>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/json/src.hpp>
#include <boost/multiprecision/cpp_dec_float.hpp>
#include <boost/program_options.hpp>
#include <chrono>
#include <fmt/base.h>
#include <fmt/ranges.h>
#include <iostream>
#include <iterator>
#include <spdlog/fmt/ranges.h>
#include <spdlog/spdlog.h>
#include <thread>

using namespace boost::multiprecision;
using namespace boost::asio;
using namespace boost::beast;
using namespace boost::json;
namespace ssl = boost::asio::ssl;
namespace beast = boost::beast;
using boost::asio::ip::tcp;
namespace po = boost::program_options;

// Define high-precision decimal type
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

// Currency exchange rate defaults
std::map<std::string, BigFloat> exchange_rates = {
    {"USD", 1.0}, {"EUR", 0.91}, {"PHP", 55.84}};

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

// Convert response body to string
std::string buffers_to_string(beast::multi_buffer::const_buffers_type buffers) {
  std::string result;
  for (auto const &buffer : buffers) {
    result.append(static_cast<const char *>(buffer.data()), buffer.size());
  }
  return result;
}

// Fetch exchange rates with redirect handling for HTTP and HTTPS
void fetch_exchange_rates() {
  try {
    net::io_context io_context;
    ssl::context ssl_ctx(ssl::context::tlsv13_client);
    // Enable peer verification
    ssl_ctx.set_verify_mode(ssl::verify_peer);

    // Load system CA certificates (e.g., from ca-certificates)
    // On Linux, this is typically /etc/ssl/certs
    ssl_ctx.set_default_verify_paths(); // Uses system's default CA certificate
                                        // paths

    // Optionally, set a custom verify callback for additional checks
    ssl_ctx.set_verify_callback([](bool preverified, ssl::verify_context &ctx) {
      // You can add custom verification logic here
      // Return preverified to use default verification result
      return preverified;
    });

    tcp::resolver resolver(io_context);

    int max_redirects = 5;
    int redirect_count = 0;

    std::string initial_url = "https://api.exchangerate-api.com/v4/latest/USD";
    Url current_url = Url::parse_url(initial_url);

    while (redirect_count < max_redirects) {
      bool is_https = current_url.protocol == "https";

      // Resolve and connect
      auto const results = resolver.resolve(current_url.host, current_url.port);

      if (is_https) {
        // HTTPS connection
        ssl::stream<tcp::socket> stream(io_context, ssl_ctx);
        net::connect(stream.next_layer(), results.begin(), results.end());
        stream.handshake(ssl::stream_base::client);

        http::request<http::string_body> req;
        req.method(http::verb::get);
        req.target(current_url.target);
        req.set(http::field::host, current_url.host);
        req.set(http::field::user_agent, "Boost.Beast/1.0");

        // Send the request
        http::write(stream, req);

        // Receive the response
        flat_buffer buffer;
        http::response<http::dynamic_body> res;
        http::read(stream, buffer, res);

        // Check for redirect
        if (res.result() == http::status::moved_permanently ||
            res.result() == http::status::found ||
            res.result() == http::status::temporary_redirect ||
            res.result() == http::status::permanent_redirect) {
          if (res.find(http::field::location) == res.end()) {
            throw std::runtime_error(
                "Redirect response missing Location header");
          }
          std::string location = std::string(res[http::field::location]);
          current_url = Url::parse_url(location);
          redirect_count++;
          continue;
        }

        // Process non-redirect response
        if (res.result() != http::status::ok) {
          throw std::runtime_error("Request failed with status: " +
                                   std::to_string((int)res.result()));
        }

        // Parse the response body as JSON
        std::string body = buffers_to_string(res.body().data());
        SPDLOG_TRACE("API response: {}", body);
        boost::json::value json_data = boost::json::parse(body);

        auto rates = json_data.as_object().at("rates").as_object();
        for (auto &rate : rates) {
          exchange_rates[rate.key()] =
              boost::json::value_to<BigFloat>(rate.value());
        }
        SPDLOG_INFO("Exchange rates updated successfully.");
        break;
      } else {
        // HTTP connection
        tcp::socket socket(io_context);
        net::connect(socket, results.begin(), results.end());

        http::request<http::string_body> req;
        req.method(http::verb::get);
        req.target(current_url.target);
        req.set(http::field::host, current_url.host);
        req.set(http::field::user_agent, "Boost.Beast/1.0");

        // Send the request
        http::write(socket, req);

        // Receive the response
        flat_buffer buffer;
        http::response<http::dynamic_body> res;
        http::read(socket, buffer, res);

        // Close the socket
        socket.shutdown(tcp::socket::shutdown_both);
        socket.close();

        // Check for redirect
        if (res.result() == http::status::moved_permanently ||
            res.result() == http::status::found ||
            res.result() == http::status::temporary_redirect ||
            res.result() == http::status::permanent_redirect) {
          if (res.find(http::field::location) == res.end()) {
            throw std::runtime_error(
                "Redirect response missing Location header");
          }
          std::string location = std::string(res[http::field::location]);
          current_url = Url::parse_url(location);
          redirect_count++;
          continue;
        }

        // Process non-redirect response
        if (res.result() != http::status::ok) {
          throw std::runtime_error("Request failed with status: " +
                                   std::to_string((int)res.result()));
        }

        // Parse the response body as JSON
        std::string body = buffers_to_string(res.body().data());
        SPDLOG_TRACE("API response: {}", body);
        boost::json::value json_data = boost::json::parse(body);

        auto rates = json_data.as_object().at("rates").as_object();
        for (auto &rate : rates) {
          exchange_rates[rate.key()] =
              boost::json::value_to<BigFloat>(rate.value());
        }
        SPDLOG_INFO("Exchange rates updated successfully.");
        break;
      }
    }

    if (redirect_count >= max_redirects) {
      throw std::runtime_error("Maximum redirects exceeded");
    }
  } catch (const std::exception &e) {
    std::cerr << "[ERROR] Error fetching exchange rates: " << e.what() << "\n";
  }
}

int main(int argc, char **argv) {
  spdlog::set_level(spdlog::level::trace);
  fmt::print("Currency Converter version 1.3\n");

  po::options_description desc("Allowed options");
  desc.add_options()("help,h", "Produce help message");

  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);
  po::notify(vm);

  if (vm.count("help")) {
    std::cout << desc << "\n";
    return 1;
  }

  fetch_exchange_rates();

  return 0;
}
