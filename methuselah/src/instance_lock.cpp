#include "methuselah/instance_lock.hpp"

#include "config.h"

#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <sys/file.h>
#include <unistd.h>

namespace methuselah {
namespace {

constexpr const char kLockFile[] = "/tmp/" PROJECT_NAME ".lock";

} // namespace

InstanceLock::InstanceLock() {
  fd_ = ::open(kLockFile, O_CREAT | O_RDWR, 0666);
  if (fd_ < 0) {
    acquired_ = false;
    return;
  }

  if (::flock(fd_, LOCK_EX | LOCK_NB) == 0) {
    acquired_ = true;
    return;
  }

  ::close(fd_);
  fd_ = -1;
  acquired_ = false;
  std::system("wmctrl -a \"" PROJECT_NAME "\"");
  std::cerr << "Application is already running. Focused existing instance."
            << std::endl;
}

InstanceLock::~InstanceLock() {
  if (fd_ < 0) {
    return;
  }
  ::unlink(kLockFile);
  ::flock(fd_, LOCK_UN);
  ::close(fd_);
  fd_ = -1;
}

} // namespace methuselah
