#pragma once
#include <cstdint>
#include <cstddef>

// Generate offline and replace before treating this as more than a bench
// test — same procedure as before, but now you have openssl ON the device
// too if you want to regenerate at provisioning time:
//
//   openssl ecparam -genkey -name prime256v1 -noout -out attest_key.pem
//   openssl req -new -x509 -key attest_key.pem -out attest_cert.pem \
//       -days 3650 -subj "/O=YourOrg/CN=Luckfox FIDO Key Batch 1"
//   openssl x509 -in attest_cert.pem -outform DER -out attest_cert.der
//   xxd -i attest_cert.der
//
// Extract the raw 32-byte private scalar from attest_key.pem similarly, or
// just load the PEM directly at runtime with OpenSSL instead of hardcoding
// bytes — since this build has a real filesystem, that's arguably cleaner:
// store attest_key.pem and attest_cert.der as separate files under
// /etc/fido/ (mode 600 for the key) and read them in main() instead of
// compiling them in. This header is provided as the simplest drop-in path.

static const uint8_t ATTESTATION_CERT_DER[] = { 0x00 };
static const size_t ATTESTATION_CERT_DER_LEN = 0;

static const uint8_t ATTESTATION_PRIV_KEY[32] = {
    0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
    0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10,
    0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,
    0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,0x20,
};
