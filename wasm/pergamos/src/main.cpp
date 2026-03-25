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
    /* DEBUGONLY:
    std::printf("myRngFunc");
    std::fflush(stdout);
    */
    static unsigned long state = 0xA5A5A5A5UL;
    for (word32 i = 0; i < sz; i++) {
        state  = state * 1103515245UL + 12345UL;
        output[i] = (byte)(state >> 24);
    }
    return 0;
}

std::string generateIotDeviceCertificate(const std::string& commonName = "iot-device-001")
{
    WC_RNG rng{};
    ecc_key deviceKey{};
    Cert cert{};
    std::vector<unsigned char> der(8192);
    std::vector<unsigned char> pem(16384);
    int ret;

    /* DEBUGONLY:
    std::printf("debug:%d,%d\n",wc_ecc_get_curve_id_from_name("SECP256R1"),
    wc_ecc_get_curve_size_from_id(ECC_SECP256R1));
    std::fflush(stdout);
    */

    ret = wolfCrypt_Init();
    if (ret != 0) {
        return "ERROR: wolfCrypt_Init failed (" + std::to_string(ret) + ")";
    }

    byte testbuf[32];
    ret = wc_RNG_GenerateBlock(&rng, testbuf, sizeof(testbuf));

    // Explicitly seed the RNG using our custom function before init
    ret = wc_InitRngNonce(&rng, NULL, 0);
    if (ret != 0) {
        // fallback: standard init which will call myRngFunc via CUSTOM_RAND_GENERATE_BLOCK
        ret = wc_InitRng_ex(&rng, NULL, INVALID_DEVID);
        if (ret != 0) {
            wolfCrypt_Cleanup();
            return "ERROR: wc_InitRng_ex failed (" + std::to_string(ret) + ")";
        }
    }

    ret = wc_ecc_init(&deviceKey);
    if (ret != 0) {
        wc_FreeRng(&rng);
        wolfCrypt_Cleanup();
        return "ERROR: wc_ecc_init failed (" + std::to_string(ret) + ")";
    }

    // Pass rng so wolfSSL calls myRngFunc internally via CUSTOM_RAND_GENERATE_BLOCK
    //ret = wc_ecc_make_key(&rng, 32, &deviceKey);
    ret = wc_ecc_make_key_ex(&rng, 32, &deviceKey, ECC_SECP256R1);
    if (ret != 0) {
        wc_ecc_free(&deviceKey);
        wc_FreeRng(&rng);
        wolfCrypt_Cleanup();
        return "ERROR: wc_ecc_make_key failed (" + std::to_string(ret) + ")";
    }

    wc_InitCert(&cert);
    strncpy(cert.subject.country,    "US",           CTC_NAME_SIZE);
    strncpy(cert.subject.org,        "MyIoTCompany", CTC_NAME_SIZE);
    strncpy(cert.subject.unit,       "IoT Devices",  CTC_NAME_SIZE);
    strncpy(cert.subject.commonName, commonName.c_str(), CTC_NAME_SIZE - 1);
    cert.isCA      = 0;
    cert.sigType   = CTC_SHA256wECDSA;
    cert.daysValid = 365 * 5;

    int derSz = wc_MakeCert(&cert, der.data(), (word32)der.size(), nullptr, &deviceKey, &rng);
    if (derSz < 0) {
        wc_ecc_free(&deviceKey);
        wc_FreeRng(&rng);
        wolfCrypt_Cleanup();
        return "ERROR: wc_MakeCert failed (" + std::to_string(derSz) + ")";
    }

    derSz = wc_SignCert(cert.bodySz, cert.sigType, der.data(), (word32)der.size(),
                        nullptr, &deviceKey, &rng);
    if (derSz < 0) {
        wc_ecc_free(&deviceKey);
        wc_FreeRng(&rng);
        wolfCrypt_Cleanup();
        return "ERROR: wc_SignCert failed (" + std::to_string(derSz) + ")";
    }

    int pemSz = wc_DerToPem(der.data(), (word32)derSz, pem.data(), (word32)pem.size(), CERT_TYPE);
    if (pemSz < 0) {
        wc_ecc_free(&deviceKey);
        wc_FreeRng(&rng);
        wolfCrypt_Cleanup();
        return "ERROR: wc_DerToPem failed (" + std::to_string(pemSz) + ")";
    }

    wc_ecc_free(&deviceKey);
    wc_FreeRng(&rng);
    wolfCrypt_Cleanup();
    return std::string(reinterpret_cast<const char*>(pem.data()), pemSz);
}

std::string read_line_stdin()
{
    std::string line;
    line.reserve(64);
    int ch;
    while ((ch = getchar()) != EOF && ch != '\n' && ch != '\r') {
        line.push_back(static_cast<char>(ch));
    }
    // discard possible trailing '\r' or '\n'
    if (ch == '\r') {
        int nxt = getchar();
        if (nxt != '\n') ungetc(nxt, stdin);
    }
    return line;
}

extern "C" {
  // WASI‑compatible entry point
  void _start() {
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
    std::printf("%s,%s,%s,%s\n", "Project", "Pergamos", "_start()", LIBWOLFSSL_VERSION_STRING);

    std::string cert = generateIotDeviceCertificate("my-iot-sensor-wasi");
    if (cert.rfind("ERROR:", 0) == 0) {
      std::printf("ERROR: %s\n", cert.c_str());
    }

    std::printf("%s\n", cert.c_str());

    std::fflush(stdout);
    std::fflush(stderr);
  }
}
