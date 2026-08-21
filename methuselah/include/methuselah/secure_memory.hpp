#pragma once

#include <array>
#include <cstddef>
#include <string.h>
#include <string>

namespace methuselah {

inline void secure_clear(void *ptr, std::size_t n) noexcept {
  if (ptr != nullptr && n > 0) {
    explicit_bzero(ptr, n);
  }
}

inline void secure_clear(std::string &s) noexcept {
  if (!s.empty()) {
    explicit_bzero(s.data(), s.size());
    s.clear();
  }
}

template <std::size_t N> class SecureArray {
public:
  SecureArray() = default;
  ~SecureArray() { secure_clear(data_.data(), data_.size()); }

  SecureArray(const SecureArray &) = delete;
  SecureArray &operator=(const SecureArray &) = delete;
  SecureArray(SecureArray &&) = delete;
  SecureArray &operator=(SecureArray &&) = delete;

  [[nodiscard]] char *data() noexcept { return data_.data(); }
  [[nodiscard]] const char *data() const noexcept { return data_.data(); }
  [[nodiscard]] int size() const noexcept { return static_cast<int>(N); }

private:
  std::array<char, N> data_{};
};

} // namespace methuselah
