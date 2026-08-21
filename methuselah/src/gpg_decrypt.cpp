#include "methuselah/gpg_decrypt.hpp"

#include <cerrno>
#include <gpgme.h>
#include <string>
#include <unistd.h>

namespace methuselah {
namespace {

gpgme_error_t passphrase_callback(void *hook, const char * /*uid_hint*/,
                                  const char * /*passphrase_info*/,
                                  int /*prev_was_bad*/, int fd) {
  const auto *passphrase = static_cast<const std::string *>(hook);
  std::string with_newline = *passphrase + "\n";
  const char *ptr = with_newline.c_str();
  std::size_t remaining = with_newline.size();
  gpgme_error_t result = GPG_ERR_NO_ERROR;
  while (remaining > 0) {
    ssize_t written = write(fd, ptr, remaining);
    if (written < 0) {
      result = gpgme_error_from_errno(errno);
      break;
    }
    ptr += written;
    remaining -= static_cast<std::size_t>(written);
  }
  secure_clear(with_newline);
  return result;
}

class GpgmeContext {
public:
  GpgmeContext() { err_ = gpgme_new(&ctx_); }
  ~GpgmeContext() {
    if (ctx_ != nullptr) {
      gpgme_release(ctx_);
    }
  }

  GpgmeContext(const GpgmeContext &) = delete;
  GpgmeContext &operator=(const GpgmeContext &) = delete;
  GpgmeContext(GpgmeContext &&) = delete;
  GpgmeContext &operator=(GpgmeContext &&) = delete;

  [[nodiscard]] bool ok() const noexcept { return ctx_ != nullptr && !err_; }
  [[nodiscard]] gpgme_error_t error() const noexcept { return err_; }
  [[nodiscard]] gpgme_ctx_t get() const noexcept { return ctx_; }

private:
  gpgme_ctx_t ctx_ = nullptr;
  gpgme_error_t err_ = 0;
};

class GpgmeData {
public:
  GpgmeData() = default;
  ~GpgmeData() {
    if (data_ != nullptr) {
      gpgme_data_release(data_);
    }
  }

  GpgmeData(const GpgmeData &) = delete;
  GpgmeData &operator=(const GpgmeData &) = delete;
  GpgmeData(GpgmeData &&) = delete;
  GpgmeData &operator=(GpgmeData &&) = delete;

  [[nodiscard]] gpgme_error_t from_file(const char *path) {
    return gpgme_data_new_from_file(&data_, path, 1);
  }

  [[nodiscard]] gpgme_error_t create() { return gpgme_data_new(&data_); }

  [[nodiscard]] gpgme_data_t get() const noexcept { return data_; }

private:
  gpgme_data_t data_ = nullptr;
};

} // namespace

DecryptResult GpgDecryptor::decrypt_file(const std::filesystem::path &gpg_path,
                                         const std::string &passphrase) const {
  DecryptResult result;

  gpgme_check_version(nullptr);

  GpgmeContext ctx;
  if (!ctx.ok()) {
    result.set_error(gpgme_strerror(ctx.error()));
    return result;
  }

  gpgme_set_protocol(ctx.get(), GPGME_PROTOCOL_OpenPGP);
  gpgme_set_armor(ctx.get(), 0);
  gpgme_set_pinentry_mode(ctx.get(), GPGME_PINENTRY_MODE_LOOPBACK);
  gpgme_set_passphrase_cb(ctx.get(), passphrase_callback,
                          const_cast<std::string *>(&passphrase));

  GpgmeData cipher;
  gpgme_error_t err = cipher.from_file(gpg_path.c_str());
  if (err) {
    result.set_error(gpgme_strerror(err));
    return result;
  }

  GpgmeData plain;
  err = plain.create();
  if (err) {
    result.set_error(gpgme_strerror(err));
    return result;
  }

  err = gpgme_op_decrypt(ctx.get(), cipher.get(), plain.get());
  if (err) {
    result.set_error(gpgme_strerror(err));
    return result;
  }

  gpgme_data_seek(plain.get(), 0, SEEK_SET);
  char buf[4096];
  ssize_t n = 0;
  std::string plaintext;
  while ((n = gpgme_data_read(plain.get(), buf, sizeof(buf))) > 0) {
    plaintext.append(buf, static_cast<std::size_t>(n));
  }
  secure_clear(buf, sizeof(buf));
  result.set_plaintext(std::move(plaintext));
  return result;
}

} // namespace methuselah
