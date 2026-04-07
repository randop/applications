#include <functional>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <uv.h>

static uv_loop_t* loop;

using json = nlohmann::json;

void
alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
  buf->base = new char[suggested_size];
  buf->len = suggested_size;
}

void
write_response(uv_stream_t* client, const json& response)
{
  std::string response_str = response.dump() + "\n";
  uv_write_t* write_req = new uv_write_t();
  write_req->data = new char[response_str.size()];
  std::copy(response_str.begin(),
            response_str.end(),
            static_cast<char*>(write_req->data));
  uv_buf_t write_buf =
    uv_buf_init(static_cast<char*>(write_req->data), response_str.size());
  uv_write(write_req, client, &write_buf, 1, [](uv_write_t* req, int status) {
    if (status) {
      std::cerr << "Write error: " << uv_strerror(status) << std::endl;
    }
    delete[] static_cast<char*>(req->data);
    delete req;
  });
}

void
handle_initialize(uv_stream_t* client, const json& request)
{
  std::cout << "Handling initialize" << std::endl;
// clang-format off /*JSON*/
  json response = {
    { "jsonrpc", "2.0" },
    { "id", request["id"] },
    { "result",
      { { "protocolVersion", "2024-11-05" },
        { "capabilities", { { "tools", { { "listChanged", true } } } } },
        { "serverInfo",
          { { "name", "McpServer" }, { "version", "0.1.0" } } } } }
  };
// clang-format on
  write_response(client, response);
}

void
handle_tools_list(uv_stream_t* client, const json& request)
{
  std::cout << "Handling tools/list" << std::endl;
// clang-format off /*JSON*/
  json response = {
    { "jsonrpc", "2.0" },
    { "id", request["id"] },
    { "result",
      { { "tools",
          json::array(
            { { { "name", "echo" },
                { "description", "Echo the input text" },
                { "inputSchema",
                  { { "type", "object" },
                    { "properties", { { "text", { { "type", "string" } } } } },
                    { "required", { "text" } } } } } }) } } }
  };
// clang-format on
  write_response(client, response);
}

void
handle_tools_call(uv_stream_t* client, const json& request)
{
  std::cout << "Handling tools/call" << std::endl;
  std::string tool_name = request["params"]["name"];
  if (tool_name == "echo") {
    std::string text = request["params"]["arguments"]["text"];
// clang-format off /*JSON*/
    json response = {
      { "jsonrpc", "2.0" },
      { "id", request["id"] },
      { "result",
        { { "content",
            json::array({ { { "type", "text" }, { "text", text } } }) } } }
    };
// clang-format on
    write_response(client, response);
  } else {
// clang-format off /*JSON*/
    json error_response = {
      { "jsonrpc", "2.0" },
      { "id", request["id"] },
      { "error", { { "code", -32601 }, { "message", "Method not found" } } }
    };
// clang-format on
    write_response(client, error_response);
  }
}

void
process_request(uv_stream_t* client, const std::string& data)
{
  std::cout << "Processing request: " << data << std::endl;
  try {
    json request = json::parse(data);
    if (!request.is_object() || !request.contains("method") ||
        !request["method"].is_string()) {
// clang-format off /*JSON*/
      json error_response = {
        { "jsonrpc", "2.0" },
        { "id", request.contains("id") ? request["id"] : nullptr },
        { "error", { { "code", -32600 }, { "message", "Invalid Request" } } }
      };
// clang-format on
      write_response(client, error_response);
      return;
    }
    std::string method = request["method"];
    if (method == "initialize") {
      handle_initialize(client, request);
    } else if (method == "tools/list") {
      handle_tools_list(client, request);
    } else if (method == "tools/call") {
      handle_tools_call(client, request);
    } else {
// clang-format off /*JSON*/
      json error_response = {
        { "jsonrpc", "2.0" },
        { "id", request.value("id", nullptr) },
        { "error", { { "code", -32601 }, { "message", "Method not found" } } }
      };
// clang-format on
      write_response(client, error_response);
    }
  } catch (const json::type_error& e) {
    std::cerr << "JSON type error: " << e.what() << std::endl;
// clang-format off /*JSON*/
    json error_response = {
      { "jsonrpc", "2.0" },
      { "id", nullptr },
      { "error", { { "code", -32600 }, { "message", "Invalid Request" } } }
    };
// clang-format on
    write_response(client, error_response);
  } catch (const json::parse_error& e) {
    std::cerr << "JSON parse error: " << e.what() << std::endl;
// clang-format off /*JSON*/
    json error_response = {
      { "jsonrpc", "2.0" },
      { "id", nullptr },
      { "error", { { "code", -32700 }, { "message", "Parse error" } } }
    };
// clang-format on
    write_response(client, error_response);
  }
}

void
echo_read(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf)
{
  if (nread > 0) {
    std::string data(buf->base, nread);
    process_request(client, data);
  } else if (nread < 0) {
    if (nread != UV_EOF) {
      std::cerr << "Read error: " << uv_err_name(nread) << std::endl;
    }
    uv_close(reinterpret_cast<uv_handle_t*>(client), nullptr);
  } else {
    // nread == 0, do nothing
  }

  if (buf->base) {
    delete[] buf->base;
  }
}

void
on_new_connection(uv_stream_t* server, int status)
{
  if (status < 0) {
    std::cerr << "New connection error: " << uv_strerror(status) << std::endl;
    return;
  }

  uv_tcp_t* client = new uv_tcp_t();
  uv_tcp_init(loop, client);
  if (uv_accept(server, reinterpret_cast<uv_stream_t*>(client)) == 0) {
    uv_read_start(
      reinterpret_cast<uv_stream_t*>(client), alloc_buffer, echo_read);
  } else {
    uv_close(reinterpret_cast<uv_handle_t*>(client), nullptr);
  }
}

int
main()
{
  loop = uv_default_loop();

  uv_tcp_t server;
  uv_tcp_init(loop, &server);

  struct sockaddr_in addr;
  uv_ip4_addr("0.0.0.0", 7000, &addr);
  uv_tcp_bind(&server, reinterpret_cast<const struct sockaddr*>(&addr), 0);
  int r =
    uv_listen(reinterpret_cast<uv_stream_t*>(&server), 128, on_new_connection);
  if (r) {
    std::cerr << "Listen error: " << uv_strerror(r) << std::endl;
    return 1;
  }

  std::cout << "MCP Server listening on port 7000" << std::endl;
  return uv_run(loop, UV_RUN_DEFAULT);
}
