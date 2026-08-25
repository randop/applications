#pragma once
#include <cstdint>
#include <cstddef>

constexpr size_t EC_PRIV_KEY_LEN = 32;
constexpr size_t EC_PUB_KEY_LEN  = 65; // uncompressed point: 0x04 || X(32) || Y(32)
constexpr size_t SHA256_LEN      = 32;
constexpr size_t KEY_HANDLE_LEN  = 64;

struct EcKeyPair {
    uint8_t priv[EC_PRIV_KEY_LEN];
    uint8_t pub[EC_PUB_KEY_LEN];
};

class FidoCrypto {
public:
    // Loads (or generates + persists to keyPath) the 32-byte device master
    // secret. Everything else is derived from this — no per-credential
    // private keys are ever written to disk.
    static bool begin(const char* keyPath = "/etc/fido/master.key");

    static void sha256(const uint8_t* data, size_t len, uint8_t out[SHA256_LEN]);
    static void hmacSha256(const uint8_t* key, size_t keyLen,
                            const uint8_t* data, size_t dataLen,
                            uint8_t out[SHA256_LEN]);

    // Deterministic P-256 keypair from HMAC(masterSecret, rpIdHash || nonce).
    static bool deriveKeyPair(const uint8_t rpIdHash[SHA256_LEN],
                               const uint8_t* nonce, size_t nonceLen,
                               EcKeyPair& out);

    // handle = nonce(32) || HMAC-SHA256(masterSecret, rpIdHash||nonce)(32)
    static void makeKeyHandle(const uint8_t rpIdHash[SHA256_LEN],
                               uint8_t outHandle[KEY_HANDLE_LEN]);

    static bool openKeyHandle(const uint8_t rpIdHash[SHA256_LEN],
                               const uint8_t* handle, size_t handleLen,
                               EcKeyPair& out);

    // ECDSA-sign a 32-byte digest -> DER signature. Returns sig length or 0.
    static size_t signDigest(const uint8_t priv[EC_PRIV_KEY_LEN],
                              const uint8_t digest[SHA256_LEN],
                              uint8_t* sigOut, size_t sigOutCap);

    // Monotonically increasing per-boot-persisted counter for AUTHENTICATE
    // responses (anti-clone signal relying parties check).
    static uint32_t nextCounter(const char* counterPath = "/etc/fido/counter");

private:
    static uint8_t s_masterSecret[32];
    static bool s_ready;
};
