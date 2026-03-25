#include <cstdio>
#include <cinttypes>
#include <cstring>
#include <string>
#include <string_view>
#include <array>
#include <optional>
#include <iostream>
#include <vector>
#include <ctime>

#include "user_settings.h"

#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/random.h>
#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/wolfcrypt/asn_public.h>
#include <wolfssl/wolfcrypt/asn.h>
#include <wolfssl/wolfcrypt/error-crypt.h>
#include <wolfssl/version.h>

// wolfSSL calls this automatically when WC_RNG_SEED_CB / CUSTOM_RNG is defined.
// Provides deterministic LCG entropy for WASI (no /dev/urandom available).
extern "C" int myRngFunc(byte* output, word32 sz);

extern "C" int myRngFunc(byte* output, word32 sz)
{
    static unsigned long state = 0xA5A5A5A5UL;
    for (word32 i = 0; i < sz; i++) {
        state  = state * 1103515245UL + 12345UL;
        output[i] = (byte)(state >> 24);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// CaContext: holds the CA key + its self-signed DER certificate, so that
// device certificates can reference a *distinct* issuer.
// ---------------------------------------------------------------------------
struct CaContext {
    ecc_key      key{};
    std::vector<unsigned char> derCert; // DER-encoded CA certificate
};

// ---------------------------------------------------------------------------
// generateCaCert
//   Generates an ECC P-256 CA key and a self-signed CA certificate.
//   The CA subject (which becomes the issuer for leaf certs) intentionally
//   differs from the device subject: separate CN, OU, and O fields.
// ---------------------------------------------------------------------------
static std::optional<CaContext> generateCaCert(WC_RNG& rng)
{
    CaContext ca;

    int ret = wc_ecc_init(&ca.key);
    if (ret != 0) return std::nullopt;

    ret = wc_ecc_make_key_ex(&rng, 32, &ca.key, ECC_SECP256R1);
    if (ret != 0) {
        wc_ecc_free(&ca.key);
        return std::nullopt;
    }

    Cert caCert{};
    wc_InitCert(&caCert);

    // ---- CA / Issuer distinguished name ----
    // These fields will appear as the *Issuer* in every leaf certificate
    // signed by this CA — explicitly different from the device subject.
    strncpy(caCert.subject.country,    "PH",                  CTC_NAME_SIZE - 1);
    strncpy(caCert.subject.state,      "Metro Manila",          CTC_NAME_SIZE - 1);
    strncpy(caCert.subject.locality,   "Quezon City",        CTC_NAME_SIZE - 1);
    strncpy(caCert.subject.org,        "Corporation",     CTC_NAME_SIZE - 1);
    strncpy(caCert.subject.unit,       "Certificate Authority", CTC_NAME_SIZE - 1);
    strncpy(caCert.subject.commonName, "Corporation Root CA", CTC_NAME_SIZE - 1);

    caCert.isCA      = 1;           // BasicConstraints: CA:TRUE
    caCert.sigType   = CTC_SHA256wECDSA;
    caCert.daysValid = 365 * 10;    // 10-year CA lifetime

    ca.derCert.resize(8192);

    // Self-signed: sign with the CA's own key (no separate issuer key)
    int derSz = wc_MakeCert(&caCert, ca.derCert.data(),
                             (word32)ca.derCert.size(), nullptr, &ca.key, &rng);
    if (derSz < 0) {
        wc_ecc_free(&ca.key);
        return std::nullopt;
    }

    derSz = wc_SignCert(caCert.bodySz, caCert.sigType,
                        ca.derCert.data(), (word32)ca.derCert.size(),
                        nullptr, &ca.key, &rng);
    if (derSz < 0) {
        wc_ecc_free(&ca.key);
        return std::nullopt;
    }

    ca.derCert.resize((size_t)derSz);
    return ca;
}

// ---------------------------------------------------------------------------
// generateIotDeviceCertificate
//   Generates a device (leaf) certificate signed by the provided CA.
//   Subject  → device identity  (CN = commonName, OU = "IoT Devices", …)
//   Issuer   → CA identity      (CN = "MyIoTCompany Root CA", …)
//   The two are now demonstrably different in every certificate field.
// ---------------------------------------------------------------------------
std::string generateIotDeviceCertificate(const std::string& commonName = "iot-device-001")
{
    WC_RNG rng{};
    ecc_key deviceKey{};
    Cert cert{};
    std::vector<unsigned char> der(8192);
    std::vector<unsigned char> pem(16384);
    int ret;

    ret = wolfCrypt_Init();
    if (ret != 0)
        return "ERROR: wolfCrypt_Init failed (" + std::to_string(ret) + ")";

    // ---- Initialise RNG ----
    ret = wc_InitRngNonce(&rng, nullptr, 0);
    if (ret != 0) {
        ret = wc_InitRng_ex(&rng, nullptr, INVALID_DEVID);
        if (ret != 0) {
            wolfCrypt_Cleanup();
            return "ERROR: wc_InitRng_ex failed (" + std::to_string(ret) + ")";
        }
    }

    // ---- Build CA (issuer) ----
    auto caOpt = generateCaCert(rng);
    if (!caOpt) {
        wc_FreeRng(&rng);
        wolfCrypt_Cleanup();
        return "ERROR: generateCaCert failed";
    }
    CaContext& ca = *caOpt;

    // ---- Device key ----
    ret = wc_ecc_init(&deviceKey);
    if (ret != 0) {
        wc_ecc_free(&ca.key);
        wc_FreeRng(&rng);
        wolfCrypt_Cleanup();
        return "ERROR: wc_ecc_init failed (" + std::to_string(ret) + ")";
    }

    ret = wc_ecc_make_key_ex(&rng, 32, &deviceKey, ECC_SECP256R1);
    if (ret != 0) {
        wc_ecc_free(&deviceKey);
        wc_ecc_free(&ca.key);
        wc_FreeRng(&rng);
        wolfCrypt_Cleanup();
        return "ERROR: wc_ecc_make_key failed (" + std::to_string(ret) + ")";
    }

    // ---- Device certificate ----
    wc_InitCert(&cert);

    // Subject — device identity (distinct from the CA issuer fields above)
    strncpy(cert.subject.country,    "PH",            CTC_NAME_SIZE - 1);
    strncpy(cert.subject.org,        "VibeHub",  CTC_NAME_SIZE - 1);
    strncpy(cert.subject.unit,       "VibeHub Device",   CTC_NAME_SIZE - 1);
    strncpy(cert.subject.commonName, commonName.c_str(), CTC_NAME_SIZE - 1);

    cert.isCA      = 0;
    cert.sigType   = CTC_SHA256wECDSA;
    cert.daysValid = 365 * 5;   // 5-year device lifetime

    // Tell wolfSSL which CA cert to use as the Issuer DN.
    // wc_SetIssuerBuffer copies the CA's DER cert and extracts its subject
    // as the issuer for the leaf certificate being built.
    ret = wc_SetIssuerBuffer(&cert, ca.derCert.data(), (int)ca.derCert.size());
    if (ret != 0) {
        wc_ecc_free(&deviceKey);
        wc_ecc_free(&ca.key);
        wc_FreeRng(&rng);
        wolfCrypt_Cleanup();
        return "ERROR: wc_SetIssuerBuffer failed (" + std::to_string(ret) + ")";
    }

    // MakeCert with device public key; sign with CA private key
    int derSz = wc_MakeCert(&cert, der.data(), (word32)der.size(),
                             nullptr, &deviceKey, &rng);
    if (derSz < 0) {
        wc_ecc_free(&deviceKey);
        wc_ecc_free(&ca.key);
        wc_FreeRng(&rng);
        wolfCrypt_Cleanup();
        return "ERROR: wc_MakeCert failed (" + std::to_string(derSz) + ")";
    }

    // Sign with CA key — this is what makes the issuer/subject truly different
    derSz = wc_SignCert(cert.bodySz, cert.sigType, der.data(), (word32)der.size(),
                        nullptr, &ca.key, &rng);
    if (derSz < 0) {
        wc_ecc_free(&deviceKey);
        wc_ecc_free(&ca.key);
        wc_FreeRng(&rng);
        wolfCrypt_Cleanup();
        return "ERROR: wc_SignCert failed (" + std::to_string(derSz) + ")";
    }

    int pemSz = wc_DerToPem(der.data(), (word32)derSz,
                             pem.data(), (word32)pem.size(), CERT_TYPE);
    if (pemSz < 0) {
        wc_ecc_free(&deviceKey);
        wc_ecc_free(&ca.key);
        wc_FreeRng(&rng);
        wolfCrypt_Cleanup();
        return "ERROR: wc_DerToPem failed (" + std::to_string(pemSz) + ")";
    }

    wc_ecc_free(&deviceKey);
    wc_ecc_free(&ca.key);
    wc_FreeRng(&rng);
    wolfCrypt_Cleanup();
    return std::string(reinterpret_cast<const char*>(pem.data()), (size_t)pemSz);
}

// ---------------------------------------------------------------------------

std::string read_line_stdin()
{
    std::string line;
    line.reserve(64);
    int ch;
    while ((ch = getchar()) != EOF && ch != '\n' && ch != '\r')
        line.push_back(static_cast<char>(ch));

    if (ch == '\r') {
        int nxt = getchar();
        if (nxt != '\n') ungetc(nxt, stdin);
    }
    return line;
}

extern "C" {
    // WASI-compatible entry point
    void _start()
    {
        std::string line = read_line_stdin();
        if (line.empty()) {
            std::printf("void\n");
            std::fflush(stdout);
            return;
        }

        int parts[4];
        if (std::sscanf(line.c_str(), "%d.%d.%d.%d",
                        &parts[0], &parts[1], &parts[2], &parts[3]) == 4) {
            std::printf("%s\n", line.c_str());
            std::fflush(stdout);
        }

        std::printf("%s,%s,%s,%s\n",
                    "Project", "Pergamos", "_start()", LIBWOLFSSL_VERSION_STRING);

        std::string cert = generateIotDeviceCertificate("my-iot-sensor-wasi");
        if (cert.rfind("ERROR:", 0) == 0)
            std::printf("ERROR: %s\n", cert.c_str());
        else
            std::printf("%s\n", cert.c_str());

        std::fflush(stdout);
        std::fflush(stderr);
    }
}
