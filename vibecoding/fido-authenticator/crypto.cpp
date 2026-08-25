#include "crypto.h"
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <sys/stat.h>
#include <sys/file.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/obj_mac.h>
#include <openssl/rand.h>
#include <openssl/bn.h>

uint8_t FidoCrypto::s_masterSecret[32];
bool FidoCrypto::s_ready = false;

static bool readAll(const char* path, uint8_t* buf, size_t len) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    size_t got = fread(buf, 1, len, f);
    fclose(f);
    return got == len;
}

static bool writeAllPrivate(const char* path, const uint8_t* buf, size_t len) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fchmod(fileno(f), 0600);
    size_t wrote = fwrite(buf, 1, len, f);
    fclose(f);
    return wrote == len;
}

static void ensureParentDir(const char* path) {
    // crude single-level mkdir; path is expected like "/etc/fido/master.key"
    char dir[256];
    strncpy(dir, path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = 0;
    char* slash = strrchr(dir, '/');
    if (slash) {
        *slash = 0;
        mkdir(dir, 0700);
    }
}

bool FidoCrypto::begin(const char* keyPath) {
    ensureParentDir(keyPath);
    if (!readAll(keyPath, s_masterSecret, sizeof(s_masterSecret))) {
        if (RAND_bytes(s_masterSecret, sizeof(s_masterSecret)) != 1) return false;
        if (!writeAllPrivate(keyPath, s_masterSecret, sizeof(s_masterSecret))) return false;
        fprintf(stderr, "Generated new master secret at %s (guard this file!)\n", keyPath);
    }
    s_ready = true;
    return true;
}

void FidoCrypto::sha256(const uint8_t* data, size_t len, uint8_t out[SHA256_LEN]) {
    SHA256(data, len, out);
}

void FidoCrypto::hmacSha256(const uint8_t* key, size_t keyLen,
                             const uint8_t* data, size_t dataLen,
                             uint8_t out[SHA256_LEN]) {
    unsigned int outLen = 0;
    HMAC(EVP_sha256(), key, (int)keyLen, data, dataLen, out, &outLen);
}

bool FidoCrypto::deriveKeyPair(const uint8_t rpIdHash[SHA256_LEN],
                                const uint8_t* nonce, size_t nonceLen,
                                EcKeyPair& out) {
    if (!s_ready) return false;

    uint8_t buf[9 + SHA256_LEN + 64];
    size_t off = 0;
    memcpy(buf + off, "ECDSA-KEY", 9); off += 9;
    memcpy(buf + off, rpIdHash, SHA256_LEN); off += SHA256_LEN;
    memcpy(buf + off, nonce, nonceLen); off += nonceLen;

    uint8_t seed[32];
    hmacSha256(s_masterSecret, sizeof(s_masterSecret), buf, off, seed);

    bool ok = false;
    EC_GROUP* grp = EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1);
    BIGNUM* order = BN_new();
    BIGNUM* d = BN_bin2bn(seed, 32, nullptr);
    EC_POINT* Q = EC_POINT_new(grp);
    BN_CTX* ctx = BN_CTX_new();

    if (grp && order && d && Q && ctx
        && EC_GROUP_get_order(grp, order, ctx) == 1
        && BN_mod(d, d, order, ctx) == 1
        && BN_is_zero(d) == 0
        && EC_POINT_mul(grp, Q, d, nullptr, nullptr, ctx) == 1) {

        int dlen = BN_bn2binpad(d, out.priv, EC_PRIV_KEY_LEN);
        size_t plen = EC_POINT_point2oct(grp, Q, POINT_CONVERSION_UNCOMPRESSED,
                                          out.pub, EC_PUB_KEY_LEN, ctx);
        ok = (dlen == (int)EC_PRIV_KEY_LEN) && (plen == EC_PUB_KEY_LEN);
    }

    BN_CTX_free(ctx);
    EC_POINT_free(Q);
    BN_free(d);
    BN_free(order);
    EC_GROUP_free(grp);
    return ok;
}

void FidoCrypto::makeKeyHandle(const uint8_t rpIdHash[SHA256_LEN],
                                uint8_t outHandle[KEY_HANDLE_LEN]) {
    RAND_bytes(outHandle, 32);
    uint8_t buf[SHA256_LEN + 32];
    memcpy(buf, rpIdHash, SHA256_LEN);
    memcpy(buf + SHA256_LEN, outHandle, 32);
    hmacSha256(s_masterSecret, sizeof(s_masterSecret), buf, sizeof(buf), outHandle + 32);
}

bool FidoCrypto::openKeyHandle(const uint8_t rpIdHash[SHA256_LEN],
                                const uint8_t* handle, size_t handleLen,
                                EcKeyPair& out) {
    if (handleLen != KEY_HANDLE_LEN) return false;
    const uint8_t* nonce = handle;
    const uint8_t* mac = handle + 32;

    uint8_t buf[SHA256_LEN + 32];
    memcpy(buf, rpIdHash, SHA256_LEN);
    memcpy(buf + SHA256_LEN, nonce, 32);
    uint8_t expectedMac[SHA256_LEN];
    hmacSha256(s_masterSecret, sizeof(s_masterSecret), buf, sizeof(buf), expectedMac);

    uint8_t diff = 0;
    for (size_t i = 0; i < SHA256_LEN; i++) diff |= expectedMac[i] ^ mac[i];
    if (diff != 0) return false;

    return deriveKeyPair(rpIdHash, nonce, 32, out);
}

size_t FidoCrypto::signDigest(const uint8_t priv[EC_PRIV_KEY_LEN],
                               const uint8_t digest[SHA256_LEN],
                               uint8_t* sigOut, size_t sigOutCap) {
    bool ok = false;
    size_t sigLen = 0;

    EC_KEY* key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    BIGNUM* d = BN_bin2bn(priv, EC_PRIV_KEY_LEN, nullptr);
    if (key && d && EC_KEY_set_private_key(key, d) == 1) {
        ECDSA_SIG* sig = ECDSA_do_sign(digest, SHA256_LEN, key);
        if (sig) {
            unsigned char* p = sigOut;
            int len = i2d_ECDSA_SIG(sig, &p);
            if (len > 0 && (size_t)len <= sigOutCap) {
                sigLen = (size_t)len;
                ok = true;
            }
            ECDSA_SIG_free(sig);
        }
    }
    if (d) BN_free(d);
    if (key) EC_KEY_free(key);
    return ok ? sigLen : 0;
}

uint32_t FidoCrypto::nextCounter(const char* counterPath) {
    uint32_t counter = 0;
    // Open read-write, create if missing, lock to avoid races if you ever
    // multithread this — daemon is currently single-threaded so mostly belt
    // and suspenders.
    FILE* f = fopen(counterPath, "r+b");
    if (!f) {
        f = fopen(counterPath, "w+b");
        if (!f) return 1; // degrade gracefully rather than crash
        fchmod(fileno(f), 0600);
    }
    flock(fileno(f), LOCK_EX);
    rewind(f);
    if (fread(&counter, sizeof(counter), 1, f) != 1) counter = 0;
    counter++;
    rewind(f);
    fwrite(&counter, sizeof(counter), 1, f);
    fflush(f);
    flock(fileno(f), LOCK_UN);
    fclose(f);
    return counter;
}
