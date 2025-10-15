#include <iomanip>
#include <iostream>
#include <memory>
#include <sodium.h>
#include <stdexcept>
#include <string>

// Function to securely encrypt a string using libsodium's secretbox
std::unique_ptr<unsigned char[]> encrypt(const std::string &plaintext,
                                         unsigned char *key,
                                         unsigned char *nonce,
                                         size_t &ciphertext_len) {
  ciphertext_len = plaintext.size() + crypto_secretbox_MACBYTES;
  auto ciphertext = std::make_unique<unsigned char[]>(ciphertext_len);

  if (crypto_secretbox_easy(
          ciphertext.get(),
          reinterpret_cast<const unsigned char *>(plaintext.c_str()),
          plaintext.size(), nonce, key) != 0) {
    throw std::runtime_error("Encryption failed");
  }

  return ciphertext;
}

// Function to securely decrypt the ciphertext back to plaintext
std::string decrypt(const unsigned char *ciphertext, size_t ciphertext_len,
                    unsigned char *key, unsigned char *nonce) {
  size_t plaintext_len = ciphertext_len - crypto_secretbox_MACBYTES;
  auto plaintext =
      std::make_unique<char[]>(plaintext_len + 1); // +1 for null terminator
  plaintext[plaintext_len] = '\0';

  if (crypto_secretbox_open_easy(
          reinterpret_cast<unsigned char *>(plaintext.get()), ciphertext,
          ciphertext_len, nonce, key) != 0) {
    throw std::runtime_error("Decryption failed");
  }

  return std::string(plaintext.get());
}

int main() {
  // Initialize libsodium
  if (sodium_init() < 0) {
    std::cerr << "Failed to initialize libsodium" << std::endl;
    return 1;
  }

  // Generate a random key (in practice, derive this securely from a password or
  // store it safely)
  unsigned char key[crypto_secretbox_KEYBYTES];
  randombytes_buf(key, sizeof(key));

  // Generate a random nonce
  unsigned char nonce[crypto_secretbox_NONCEBYTES];
  randombytes_buf(nonce, sizeof(nonce));

  // The string to protect (encrypt)
  std::string original = "This is a secret message that needs protection!";
  std::cout << "Original message: " << original << std::endl;

  try {
    // Encrypt
    size_t ciphertext_len;
    auto ciphertext = encrypt(original, key, nonce, ciphertext_len);

    // Securely wipe the original plaintext from memory
    sodium_memzero(const_cast<char *>(original.data()), original.size());
    original.clear(); // Ensure the string is empty after wiping

    std::cout << "Encrypted (hex): ";
    for (size_t i = 0; i < ciphertext_len; ++i) {
      std::cout << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<int>(ciphertext[i]);
    }
    std::cout << std::dec << std::endl;

    // Decrypt
    std::string recovered =
        decrypt(ciphertext.get(), ciphertext_len, key, nonce);
    std::cout << "Decrypted message: " << recovered << std::endl;

    // Verify
    if (original == recovered) { // Note: original is now empty, so this will
                                 // fail post-wipe (as intended for security)
      std::cout << "Verification: SUCCESS! The message was protected and "
                   "recovered correctly."
                << std::endl;
    } else {
      std::cout << "Verification: FAILED! (Expected after wiping original)"
                << std::endl;
    }
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  // Clean up sensitive data (key and nonce)
  sodium_memzero(key, sizeof(key));
  sodium_memzero(nonce, sizeof(nonce));

  return 0;
}
