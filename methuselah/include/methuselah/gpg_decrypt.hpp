#pragma once

#include "methuselah/secure_memory.hpp"

#include <filesystem>
#include <string>
#include <utility>

namespace methuselah {

class DecryptResult {
public:
  DecryptResult() = default;
  ~DecryptResult() { secure_clear(plaintext_); }

  DecryptResult(const DecryptResult &) = delete;
  DecryptResult &operator=(const DecryptResult &) = delete;

  DecryptResult(DecryptResult &&other) noexcept
      : plaintext_(std::move(other.plaintext_)),
        error_(std::move(other.error_)) {
    other.plaintext_.clear();
  }

  DecryptResult &operator=(DecryptResult &&other) noexcept {
    if (this != &other) {
      secure_clear(plaintext_);
      plaintext_ = std::move(other.plaintext_);
      error_ = std::move(other.error_);
      other.plaintext_.clear();
    }
    return *this;
  }

  void set_plaintext(std::string text) { plaintext_ = std::move(text); }
  void set_error(std::string err) { error_ = std::move(err); }

  [[nodiscard]] bool ok() const noexcept { return error_.empty(); }
  [[nodiscard]] const std::string &plaintext() const noexcept {
    return plaintext_;
  }
  [[nodiscard]] const std::string &error() const noexcept { return error_; }

private:
  std::string plaintext_;
  std::string error_;
};

class GpgDecryptor {
public:
  [[nodiscard]] DecryptResult
  decrypt_file(const std::filesystem::path &gpg_path,
               const std::string &passphrase) const;
};

} // namespace methuselah
