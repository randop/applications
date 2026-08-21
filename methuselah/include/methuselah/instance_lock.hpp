#pragma once

namespace methuselah {

class InstanceLock {
public:
  InstanceLock();
  ~InstanceLock();

  InstanceLock(const InstanceLock &) = delete;
  InstanceLock &operator=(const InstanceLock &) = delete;
  InstanceLock(InstanceLock &&) = delete;
  InstanceLock &operator=(InstanceLock &&) = delete;

  [[nodiscard]] bool acquired() const noexcept { return acquired_; }

private:
  int fd_ = -1;
  bool acquired_ = false;
};

} // namespace methuselah
