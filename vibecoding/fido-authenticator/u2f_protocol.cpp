#include "u2f_protocol.h"
#include "crypto.h"
#include "attestation_cert.h"
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

int U2fProtocol::s_presenceGpio = -1;

static void gpioWrite(const char* path, const char* value) {
    FILE* f = fopen(path, "w");
    if (!f) return;
    fputs(value, f);
    fclose(f);
}

void U2fProtocol::begin(int presenceGpio) {
    s_presenceGpio = presenceGpio;
    if (s_presenceGpio < 0) return;

    char buf[64];
    snprintf(buf, sizeof(buf), "%d", s_presenceGpio);
    gpioWrite("/sys/class/gpio/export", buf);

    char dirPath[96];
    snprintf(dirPath, sizeof(dirPath), "/sys/class/gpio/gpio%d/direction", s_presenceGpio);
    gpioWrite(dirPath, "in");
    // Wire the button to pull the pin LOW on press; if your board's sysfs
    // gpio export doesn't default to pull-up, add an external 10k pull-up
    // to 3.3V, or check for an active-high alternative wiring and flip the
    // read logic below.
}

static bool gpioIsPressed(int gpio) {
    char valPath[96];
    snprintf(valPath, sizeof(valPath), "/sys/class/gpio/gpio%d/value", gpio);
    FILE* f = fopen(valPath, "r");
    if (!f) return false;
    char c = fgetc(f);
    fclose(f);
    return c == '0'; // active-low
}

static uint64_t nowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

bool U2fProtocol::waitForUserPresence(uint32_t timeoutMs) {
    if (s_presenceGpio < 0) return false; // no button configured = fail closed
    uint64_t start = nowMs();
    while (nowMs() - start < timeoutMs) {
        if (gpioIsPressed(s_presenceGpio)) {
            usleep(20000);
            if (gpioIsPressed(s_presenceGpio)) return true;
        }
        usleep(10000);
    }
    return false;
}

static void appendU16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back((x >> 8) & 0xFF);
    v.push_back(x & 0xFF);
}

enum U2fIns : uint8_t { INS_REGISTER = 0x01, INS_AUTHENTICATE = 0x02, INS_VERSION = 0x03 };
enum SW : uint16_t {
    SW_NO_ERROR = 0x9000, SW_CONDITIONS_NOT_SATISFIED = 0x6985,
    SW_WRONG_DATA = 0x6A80, SW_WRONG_LENGTH = 0x6700,
    SW_CLA_NOT_SUPPORTED = 0x6E00, SW_INS_NOT_SUPPORTED = 0x6D00,
};

bool U2fProtocol::handleApdu(const std::vector<uint8_t>& apdu,
                              std::vector<uint8_t>& response) {
    response.clear();
    if (apdu.size() < 4) { appendU16(response, SW_WRONG_LENGTH); return true; }

    uint8_t cla = apdu[0], ins = apdu[1], p1 = apdu[2];
    if (cla != 0x00) { appendU16(response, SW_CLA_NOT_SUPPORTED); return true; }

    size_t bodyStart = 4, lc = 0;
    if (apdu.size() >= 7 && apdu[4] == 0x00) {
        lc = (apdu[5] << 8) | apdu[6];
        bodyStart = 7;
    }
    std::vector<uint8_t> body;
    if (lc > 0 && bodyStart + lc <= apdu.size())
        body.assign(apdu.begin() + bodyStart, apdu.begin() + bodyStart + lc);

    switch (ins) {
        case INS_VERSION: {
            const char* v = "U2F_V2";
            response.assign(v, v + strlen(v));
            appendU16(response, SW_NO_ERROR);
            return true;
        }
        case INS_REGISTER: return handleRegister(body, response);
        case INS_AUTHENTICATE: return handleAuthenticate(p1, body, response);
        default: appendU16(response, SW_INS_NOT_SUPPORTED); return true;
    }
}

bool U2fProtocol::handleRegister(const std::vector<uint8_t>& body,
                                  std::vector<uint8_t>& response) {
    if (body.size() != 64) { appendU16(response, SW_WRONG_LENGTH); return true; }
    const uint8_t* challenge = body.data();
    const uint8_t* rpIdHash  = body.data() + 32;

    if (!waitForUserPresence(15000)) { appendU16(response, SW_CONDITIONS_NOT_SATISFIED); return true; }

    uint8_t keyHandle[KEY_HANDLE_LEN];
    FidoCrypto::makeKeyHandle(rpIdHash, keyHandle);

    EcKeyPair kp;
    if (!FidoCrypto::openKeyHandle(rpIdHash, keyHandle, KEY_HANDLE_LEN, kp)) {
        appendU16(response, SW_WRONG_DATA); return true;
    }

    std::vector<uint8_t> signedData;
    signedData.push_back(0x00);
    signedData.insert(signedData.end(), rpIdHash, rpIdHash + 32);
    signedData.insert(signedData.end(), challenge, challenge + 32);
    signedData.insert(signedData.end(), keyHandle, keyHandle + KEY_HANDLE_LEN);
    signedData.insert(signedData.end(), kp.pub, kp.pub + EC_PUB_KEY_LEN);

    uint8_t digest[SHA256_LEN];
    FidoCrypto::sha256(signedData.data(), signedData.size(), digest);

    uint8_t sig[80];
    size_t sigLen = FidoCrypto::signDigest(ATTESTATION_PRIV_KEY, digest, sig, sizeof(sig));
    if (sigLen == 0) { appendU16(response, SW_WRONG_DATA); return true; }

    response.push_back(0x05);
    response.insert(response.end(), kp.pub, kp.pub + EC_PUB_KEY_LEN);
    response.push_back(KEY_HANDLE_LEN);
    response.insert(response.end(), keyHandle, keyHandle + KEY_HANDLE_LEN);
    response.insert(response.end(), ATTESTATION_CERT_DER, ATTESTATION_CERT_DER + ATTESTATION_CERT_DER_LEN);
    response.insert(response.end(), sig, sig + sigLen);
    appendU16(response, SW_NO_ERROR);
    return true;
}

bool U2fProtocol::handleAuthenticate(uint8_t p1, const std::vector<uint8_t>& body,
                                      std::vector<uint8_t>& response) {
    if (body.size() < 65) { appendU16(response, SW_WRONG_LENGTH); return true; }
    const uint8_t* challenge = body.data();
    const uint8_t* rpIdHash  = body.data() + 32;
    uint8_t khLen = body[64];
    if (body.size() != 65 + khLen) { appendU16(response, SW_WRONG_LENGTH); return true; }
    const uint8_t* keyHandle = body.data() + 65;

    EcKeyPair kp;
    bool valid = FidoCrypto::openKeyHandle(rpIdHash, keyHandle, khLen, kp);

    if (p1 == 0x07) { // check-only
        appendU16(response, valid ? SW_CONDITIONS_NOT_SATISFIED : SW_WRONG_DATA);
        return true;
    }
    if (!valid) { appendU16(response, SW_WRONG_DATA); return true; }
    if (!waitForUserPresence(15000)) { appendU16(response, SW_CONDITIONS_NOT_SATISFIED); return true; }

    uint32_t counter = FidoCrypto::nextCounter(); // real persisted, incrementing counter

    uint8_t userPresence = 0x01;
    std::vector<uint8_t> signedData;
    signedData.insert(signedData.end(), rpIdHash, rpIdHash + 32);
    signedData.push_back(userPresence);
    signedData.push_back((counter >> 24) & 0xFF);
    signedData.push_back((counter >> 16) & 0xFF);
    signedData.push_back((counter >> 8) & 0xFF);
    signedData.push_back(counter & 0xFF);
    signedData.insert(signedData.end(), challenge, challenge + 32);

    uint8_t digest[SHA256_LEN];
    FidoCrypto::sha256(signedData.data(), signedData.size(), digest);

    uint8_t sig[80];
    size_t sigLen = FidoCrypto::signDigest(kp.priv, digest, sig, sizeof(sig));
    if (sigLen == 0) { appendU16(response, SW_WRONG_DATA); return true; }

    response.push_back(userPresence);
    response.push_back((counter >> 24) & 0xFF);
    response.push_back((counter >> 16) & 0xFF);
    response.push_back((counter >> 8) & 0xFF);
    response.push_back(counter & 0xFF);
    response.insert(response.end(), sig, sig + sigLen);
    appendU16(response, SW_NO_ERROR);
    return true;
}
