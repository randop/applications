#include "app.h"

// Convert response body to string
std::string buffers_to_string(beast::multi_buffer::const_buffers_type buffers) {
  std::string result;
  for (auto const &buffer : buffers) {
    result.append(static_cast<const char *>(buffer.data()), buffer.size());
  }
  return result;
}

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
