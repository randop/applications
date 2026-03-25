#pragma once

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <sstream>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <unordered_set>

using string = std::string;

class Environment {
public:
  static void logOSinfo();

private:
  static string formatBytes(uint64_t bytes);
};

string Environment::formatBytes(uint64_t bytes) {
  const char *kUnits[] = {"B", "KB", "MB", "GB", "TB"};
  int unitIndex = 0;
  double size = static_cast<double>(bytes);

  while (size >= 1024 && unitIndex < 4) {
    size /= 1024;
    ++unitIndex;
  }

  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2) << size << " " << kUnits[unitIndex];
  return oss.str();
}

void Environment::logOSinfo() {
  struct utsname info;
  if (uname(&info) == -1) {
    std::cerr << "OS info error: " << std::strerror(errno) << std::endl;
  } else {
    std::cout << "OS info: " << info.sysname << " " << info.nodename << " " << info.release << " " << info.machine << std::endl;
  }

  // Get memory info
  struct sysinfo mem_info;
  if (sysinfo(&mem_info) == -1) {
    std::cerr << "Error retrieving memory info: " << std::strerror(errno) << std::endl;
  } else {
    // Print total memory
    uint64_t total_memory = mem_info.totalram * mem_info.mem_unit;
    std::cout << "RAM: " << formatBytes(total_memory) << std::endl;
  }
}


