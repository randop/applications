#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include <boost/program_options.hpp>

#include "app.h"

namespace po = boost::program_options;

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
