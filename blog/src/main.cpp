/***
###############################################################################
# Includes
###############################################################################
***/

#include "include/environment.hpp"
#include "include/httpServer.hpp"
#include "include/page.hpp"
#include "include/post.hpp"
#include "project.hpp"

#include <boost/program_options.hpp>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>

#include <bsoncxx/json.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/uri.hpp>

/***
###############################################################################
# Constants
###############################################################################
***/
#include "include/constants.h"

/***
###############################################################################
# Namespaces
###############################################################################
***/

namespace po = boost::program_options;

int main(int argc, char *argv[]) {
  try {
    po::options_description desc("Allowed options");
    desc.add_options()("help,h", "Produce help message")(
        "version,v", "Print version information");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
      std::cout << desc << std::endl;
      return EXIT_SUCCESS;
    }

    if (vm.count("version")) {
      std::cout << PROJECT_VERSION << std::endl;
      return EXIT_SUCCESS;
    }
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  spdlog::info("Blog server project: {} (build: {})", PROJECT_VERSION,
               PROJECT_BUILD);
  Environment::logOSinfo();

  auto docRoot = std::make_shared<std::string>("/var/www/html");
  if (auto envDocRoot = Environment::getVariable("DOC_ROOT")) {
    docRoot = std::make_shared<std::string>(envDocRoot.value());
    spdlog::info("DOC_ROOT => {}", docRoot->c_str());
  } else {
    spdlog::warn("Unspecified environment variable DOC_ROOT using default: {}",
                 docRoot->c_str());
  }

  std::string mongoDbUrl = "mongodb+srv://user:password@host/"
                           "?retryWrites=true&w=majority&appName=app";
  if (auto envMongoDbUrl = Environment::getVariable("MONGODB_URL")) {
    spdlog::debug("MONGODB_URL => {}", envMongoDbUrl.value());
    mongoDbUrl = envMongoDbUrl.value();
  } else {
    spdlog::warn(
        "Unspecified environment variable MONGODB_URL using default: {}",
        mongoDbUrl);
  }

  std::shared_ptr<mongocxx::pool> mongoDbPool;

  try {
    // Initialize the MongoDB C++ driver
    mongocxx::instance inst{};
    mongocxx::uri uri(mongoDbUrl);

    // Set the version of the Stable API on the client
    mongocxx::options::client client_options;
    const auto api = mongocxx::options::server_api{
        mongocxx::options::server_api::version::k_version_1};
    client_options.server_api_opts(api);

    mongoDbPool = std::make_shared<mongocxx::pool>(uri, client_options);
    auto conn = mongoDbPool->acquire();
    mongocxx::database db = conn["admin"];

    // Ping the database.
    const auto ping_cmd = bsoncxx::builder::basic::make_document(
        bsoncxx::builder::basic::kvp("ping", 1));
    db.run_command(ping_cmd.view());
    spdlog::info("Bootstrap mongo database connection: OK");
  } catch (const std::exception &e) {
    // Handle errors
    spdlog::error("mongodb error: {}", e.what());
    return EXIT_FAILURE;
  }

  const char *host = ANY_IPV4_HOST;
  auto const address = net::ip::make_address(host);
  auto const port = static_cast<unsigned short>(DEFAULT_PORT);
  auto const threadCount = std::max<int>(1, 4);

  auto post = std::make_shared<blog::Post>(mongoDbPool);
  auto page = std::make_shared<blog::Page>(mongoDbPool);

  // The io_context is required for all I/O
  net::io_context ioc{threadCount};

  // Create and launch a listening port
  std::make_shared<listener>(ioc, tcp::endpoint{address, port}, docRoot, post,
                             page)
      ->run();

  spdlog::info("http server listening on {} port {}", host, port);

  // Run the I/O service on the requested number of threads
  std::vector<std::thread> threads;
  threads.reserve(threadCount - 1);
  for (auto i = threadCount - 1; i > 0; --i) {
    threads.emplace_back([&ioc] { ioc.run(); });
  }
  ioc.run();

  return EXIT_SUCCESS;
}
