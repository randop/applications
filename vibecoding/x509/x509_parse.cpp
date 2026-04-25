/**
 * x509_parse.cpp — C++23, libgnutls 3.x
 *
 * Parse an X.509 PEM certificate into a strongly-typed struct.
 *
 * Build:
 *   g++ -std=c++23 -Wall -Wextra -o x509_parse x509_parse.cpp $(pkg-config --cflags --libs gnutls)
 *
 * Run:
 *   ./x509_parse cert.pem
 *   ./x509_parse                   # reads from stdin
 */

#include <gnutls/gnutls.h>
#include <gnutls/x509.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <expected>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// ── println shim (std::println needs GCC 14) ──────────────────────────────────
template<typename... Args>
static void println(std::FILE* f, std::format_string<Args...> fmt, Args&&... args)
{
    std::fputs((std::format(fmt, std::forward<Args>(args)...) + '\n').c_str(), f);
}
template<typename... Args>
static void println(std::format_string<Args...> fmt, Args&&... args)
{ println(stdout, fmt, std::forward<Args>(args)...); }
static void println(std::string_view s) { std::cout << s << '\n'; }

// ── join helper (std::ranges::to not in GCC 13) ──────────────────────────────
[[nodiscard]] static std::string join(const std::vector<std::string>& v,
                                      std::string_view sep)
{
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) out += sep;
        out += v[i];
    }
    return out;
}

// ── RAII cert wrapper ─────────────────────────────────────────────────────────
struct CrtGuard {
    gnutls_x509_crt_t crt{};
    CrtGuard()  { gnutls_x509_crt_init(&crt); }
    ~CrtGuard() { gnutls_x509_crt_deinit(crt); }
    CrtGuard(const CrtGuard&)            = delete;
    CrtGuard& operator=(const CrtGuard&) = delete;
    CrtGuard(CrtGuard&&)                 = delete;
    CrtGuard& operator=(CrtGuard&&)      = delete;
};

// ── Data structures ───────────────────────────────────────────────────────────
struct DistinguishedName {
    std::string common_name;
    std::string organization;
    std::string organizational_unit;
    std::string country;
    std::string state;
    std::string locality;
    std::string email;
    std::string raw_dn;
};

struct PublicKeyInfo {
    std::string algorithm;
    unsigned    bits{};
    std::string curve;
};

struct Extension {
    std::string oid;
    bool        critical{};
    std::string value_hex;
};

struct X509CertInfo {
    DistinguishedName subject;
    DistinguishedName issuer;
    std::string       serial_hex;
    std::chrono::system_clock::time_point not_before;
    std::chrono::system_clock::time_point not_after;
    std::vector<std::string> san_dns;
    std::vector<std::string> san_ip;
    std::vector<std::string> san_email;
    std::vector<std::string> san_uri;
    PublicKeyInfo             public_key;
    std::string               signature_algorithm;
    bool     is_ca{};
    int      path_len_constraint{-1};
    unsigned key_usage{};
    std::vector<std::string> extended_key_usage;
    std::string fingerprint_sha1;
    std::string fingerprint_sha256;
    unsigned    version{};
    bool        self_signed{};
    std::vector<Extension> extensions;
};

// ── Byte helpers ──────────────────────────────────────────────────────────────
[[nodiscard]] static std::string to_hex(std::span<const unsigned char> buf)
{
    std::string out;
    out.reserve(buf.size() * 2);
    for (unsigned char b : buf)
        std::format_to(std::back_inserter(out), "{:02x}", b);
    return out;
}

[[nodiscard]] static std::string to_hex_colon(std::span<const unsigned char> buf)
{
    std::string out;
    out.reserve(buf.size() * 3);
    for (std::size_t i = 0; i < buf.size(); ++i) {
        if (i) out += ':';
        std::format_to(std::back_inserter(out), "{:02X}", buf[i]);
    }
    return out;
}

// ── gnutls helpers ────────────────────────────────────────────────────────────
[[nodiscard]] static std::string gnutls_err(std::string_view ctx, int rc)
{
    return std::format("{}: {} ({})", ctx, gnutls_strerror(rc), rc);
}

[[nodiscard]] static std::string fingerprint(gnutls_x509_crt_t crt,
                                             gnutls_digest_algorithm_t algo)
{
    std::vector<unsigned char> buf(64);
    std::size_t sz = buf.size();
    int rc = gnutls_x509_crt_get_fingerprint(crt, algo, buf.data(), &sz);
    if (rc != GNUTLS_E_SUCCESS) return std::format("<err:{}>", rc);
    buf.resize(sz);
    return to_hex_colon(buf);
}

[[nodiscard]] static std::string dn_attr(gnutls_x509_crt_t crt, bool subject,
                                         const char* oid, unsigned idx = 0)
{
    std::string buf(256, '\0');
    std::size_t sz = buf.size();
    int rc = subject
        ? gnutls_x509_crt_get_dn_by_oid(crt, oid, idx, 0, buf.data(), &sz)
        : gnutls_x509_crt_get_issuer_dn_by_oid(crt, oid, idx, 0, buf.data(), &sz);
    if (rc < 0) return {};
    buf.resize(sz);
    return buf;
}

[[nodiscard]] static std::string full_dn(gnutls_x509_crt_t crt, bool subject)
{
    std::string buf(512, '\0');
    std::size_t sz = buf.size();
    int rc = subject ? gnutls_x509_crt_get_dn(crt, buf.data(), &sz)
                     : gnutls_x509_crt_get_issuer_dn(crt, buf.data(), &sz);
    if (rc < 0) return {};
    buf.resize(sz);
    return buf;
}

[[nodiscard]] static DistinguishedName parse_dn(gnutls_x509_crt_t crt, bool subject)
{
    DistinguishedName dn;
    dn.common_name         = dn_attr(crt, subject, GNUTLS_OID_X520_COMMON_NAME);
    dn.organization        = dn_attr(crt, subject, GNUTLS_OID_X520_ORGANIZATION_NAME);
    dn.organizational_unit = dn_attr(crt, subject, GNUTLS_OID_X520_ORGANIZATIONAL_UNIT_NAME);
    dn.country             = dn_attr(crt, subject, GNUTLS_OID_X520_COUNTRY_NAME);
    dn.state               = dn_attr(crt, subject, GNUTLS_OID_X520_STATE_OR_PROVINCE_NAME);
    dn.locality            = dn_attr(crt, subject, GNUTLS_OID_X520_LOCALITY_NAME);
    dn.email               = dn_attr(crt, subject, GNUTLS_OID_PKCS9_EMAIL);
    dn.raw_dn              = full_dn(crt, subject);
    return dn;
}

// ── SANs ──────────────────────────────────────────────────────────────────────
static void collect_sans(gnutls_x509_crt_t crt, X509CertInfo& info)
{
    for (unsigned seq = 0; ; ++seq) {
        std::string buf(256, '\0');
        std::size_t sz       = buf.size();
        unsigned    san_type{};
        int rc = gnutls_x509_crt_get_subject_alt_name(
                     crt, seq, buf.data(), &sz, &san_type);
        if (rc == GNUTLS_E_REQUESTED_DATA_NOT_AVAILABLE) break;
        if (rc == GNUTLS_E_SHORT_MEMORY_BUFFER) {
            buf.resize(sz);
            rc = gnutls_x509_crt_get_subject_alt_name(
                     crt, seq, buf.data(), &sz, &san_type);
        }
        if (rc < 0) break;
        buf.resize(sz);
        switch (san_type) {
            case GNUTLS_SAN_DNSNAME:    info.san_dns.push_back(buf);   break;
            case GNUTLS_SAN_IPADDRESS:  info.san_ip.push_back(buf);    break;
            case GNUTLS_SAN_RFC822NAME: info.san_email.push_back(buf); break;
            case GNUTLS_SAN_URI:        info.san_uri.push_back(buf);   break;
            default: break;
        }
    }
}

// ── Extended key usage ────────────────────────────────────────────────────────
[[nodiscard]] static std::string eku_oid_name(std::string_view oid)
{
    static constexpr std::pair<std::string_view, std::string_view> map[] = {
        { "1.3.6.1.5.5.7.3.1",      "TLS Web Server Authentication" },
        { "1.3.6.1.5.5.7.3.2",      "TLS Web Client Authentication" },
        { "1.3.6.1.5.5.7.3.3",      "Code Signing" },
        { "1.3.6.1.5.5.7.3.4",      "Email Protection" },
        { "1.3.6.1.5.5.7.3.8",      "Time Stamping" },
        { "1.3.6.1.5.5.7.3.9",      "OCSP Signing" },
        { "2.5.29.37.0",             "Any Extended Key Usage" },
        { "1.3.6.1.4.1.311.10.3.3", "Microsoft SGC" },
        { "2.16.840.1.113730.4.1",   "Netscape SGC" },
    };
    for (auto& [k, v] : map)
        if (k == oid) return std::string{v};
    return std::string{oid};
}

static void collect_eku(gnutls_x509_crt_t crt, X509CertInfo& info)
{
    for (unsigned seq = 0; ; ++seq) {
        std::string buf(128, '\0');
        std::size_t sz = buf.size();
        int rc = gnutls_x509_crt_get_key_purpose_oid(
                     crt, seq, buf.data(), &sz, nullptr);
        if (rc == GNUTLS_E_REQUESTED_DATA_NOT_AVAILABLE) break;
        if (rc == GNUTLS_E_SHORT_MEMORY_BUFFER) {
            buf.resize(sz);
            rc = gnutls_x509_crt_get_key_purpose_oid(
                     crt, seq, buf.data(), &sz, nullptr);
        }
        if (rc < 0) break;
        buf.resize(sz);
        info.extended_key_usage.push_back(eku_oid_name(buf));
    }
}

// ── Raw extensions ────────────────────────────────────────────────────────────
static void collect_extensions(gnutls_x509_crt_t crt, X509CertInfo& info)
{
    for (unsigned seq = 0; ; ++seq) {
        char        oid_buf[256]{};
        std::size_t oid_sz  = sizeof(oid_buf);
        unsigned    critical{};
        int rc = gnutls_x509_crt_get_extension_info(
                     crt, seq, oid_buf, &oid_sz, &critical);
        if (rc == GNUTLS_E_REQUESTED_DATA_NOT_AVAILABLE) break;
        if (rc < 0) break;
        gnutls_datum_t val{ nullptr, 0 };
        rc = gnutls_x509_crt_get_extension_data2(crt, seq, &val);
        Extension ext;
        ext.oid      = oid_buf;
        ext.critical = (critical != 0);
        if (rc == GNUTLS_E_SUCCESS && val.data) {
            ext.value_hex = to_hex({ val.data, val.size });
            gnutls_free(val.data);
        }
        info.extensions.push_back(std::move(ext));
    }
}

// ── Key usage bitmask → strings ───────────────────────────────────────────────
[[nodiscard]] static std::vector<std::string> key_usage_strings(unsigned ku)
{
    std::vector<std::string> out;
    if (ku & GNUTLS_KEY_DIGITAL_SIGNATURE) out.emplace_back("Digital Signature");
    if (ku & GNUTLS_KEY_NON_REPUDIATION)   out.emplace_back("Non Repudiation");
    if (ku & GNUTLS_KEY_KEY_ENCIPHERMENT)  out.emplace_back("Key Encipherment");
    if (ku & GNUTLS_KEY_DATA_ENCIPHERMENT) out.emplace_back("Data Encipherment");
    if (ku & GNUTLS_KEY_KEY_AGREEMENT)     out.emplace_back("Key Agreement");
    if (ku & GNUTLS_KEY_KEY_CERT_SIGN)     out.emplace_back("Key Cert Sign");
    if (ku & GNUTLS_KEY_CRL_SIGN)          out.emplace_back("CRL Sign");
    if (ku & GNUTLS_KEY_ENCIPHER_ONLY)     out.emplace_back("Encipher Only");
    if (ku & GNUTLS_KEY_DECIPHER_ONLY)     out.emplace_back("Decipher Only");
    return out;
}

// ── Public key ────────────────────────────────────────────────────────────────
[[nodiscard]] static PublicKeyInfo parse_pubkey(gnutls_x509_crt_t crt)
{
    PublicKeyInfo pki;
    auto algo = static_cast<gnutls_pk_algorithm_t>(
        gnutls_x509_crt_get_pk_algorithm(crt, &pki.bits));
    switch (algo) {
        case GNUTLS_PK_RSA:           pki.algorithm = "RSA";     break;
        case GNUTLS_PK_RSA_PSS:       pki.algorithm = "RSA-PSS"; break;
        case GNUTLS_PK_DSA:           pki.algorithm = "DSA";     break;
        case GNUTLS_PK_ECDSA:         pki.algorithm = "EC";      break;
        case GNUTLS_PK_EDDSA_ED25519: pki.algorithm = "Ed25519"; pki.bits = 255; break;
        case GNUTLS_PK_EDDSA_ED448:   pki.algorithm = "Ed448";   pki.bits = 448; break;
        default: pki.algorithm = std::format("Unknown({})", static_cast<int>(algo));
    }
    if (algo == GNUTLS_PK_ECDSA) {
        gnutls_ecc_curve_t c  = GNUTLS_ECC_CURVE_INVALID;
        gnutls_datum_t     px = {nullptr, 0};
        gnutls_datum_t     py = {nullptr, 0};
        if (gnutls_x509_crt_get_pk_ecc_raw(crt, &c, &px, &py) == GNUTLS_E_SUCCESS) {
            if (c != GNUTLS_ECC_CURVE_INVALID) pki.curve = gnutls_ecc_curve_get_name(c);
            gnutls_free(px.data);
            gnutls_free(py.data);
        }
    }
    return pki;
}

// ── Main parser ───────────────────────────────────────────────────────────────
[[nodiscard]] static std::expected<X509CertInfo, std::string>
parse_pem(std::string_view pem)
{
    CrtGuard g;
    gnutls_datum_t data{
        const_cast<unsigned char*>(
            reinterpret_cast<const unsigned char*>(pem.data())),
        static_cast<unsigned>(pem.size())
    };

    int rc = gnutls_x509_crt_import(g.crt, &data, GNUTLS_X509_FMT_PEM);
    if (rc != GNUTLS_E_SUCCESS)
        return std::unexpected(gnutls_err("gnutls_x509_crt_import", rc));

    X509CertInfo info;

    info.version     = static_cast<unsigned>(gnutls_x509_crt_get_version(g.crt));
    info.subject     = parse_dn(g.crt, true);
    info.issuer      = parse_dn(g.crt, false);
    info.self_signed = (gnutls_x509_crt_check_issuer(g.crt, g.crt) > 0);

    // Serial
    {
        std::vector<unsigned char> serial(64);
        std::size_t sz = serial.size();
        if (gnutls_x509_crt_get_serial(g.crt, serial.data(), &sz) == GNUTLS_E_SUCCESS) {
            serial.resize(sz);
            info.serial_hex = to_hex(serial);
        }
    }

    // Validity
    {
        using sc = std::chrono::system_clock;
        info.not_before = sc::from_time_t(
            static_cast<std::time_t>(gnutls_x509_crt_get_activation_time(g.crt)));
        info.not_after  = sc::from_time_t(
            static_cast<std::time_t>(gnutls_x509_crt_get_expiration_time(g.crt)));
    }

    collect_sans(g.crt, info);
    info.public_key = parse_pubkey(g.crt);

    // Signature algorithm
    rc = gnutls_x509_crt_get_signature_algorithm(g.crt);
    if (rc >= 0) {
        const char* name = gnutls_sign_get_name(
            static_cast<gnutls_sign_algorithm_t>(rc));
        info.signature_algorithm = name ? name : std::format("Unknown({})", rc);
    }

    // Basic constraints
    {
        unsigned ca_flag{}; int path_len{};
        if (gnutls_x509_crt_get_basic_constraints(g.crt, nullptr, &ca_flag, &path_len) >= 0) {
            info.is_ca = (ca_flag != 0);
            info.path_len_constraint = info.is_ca ? path_len : -1;
        }
    }

    // Key usage
    {
        unsigned ku{};
        if (gnutls_x509_crt_get_key_usage(g.crt, &ku, nullptr) == GNUTLS_E_SUCCESS)
            info.key_usage = ku;
    }

    collect_eku(g.crt, info);
    info.fingerprint_sha1   = fingerprint(g.crt, GNUTLS_DIG_SHA1);
    info.fingerprint_sha256 = fingerprint(g.crt, GNUTLS_DIG_SHA256);
    collect_extensions(g.crt, info);

    return info;
}

// ── Pretty-printer ────────────────────────────────────────────────────────────
static void print_dn(std::string_view label, const DistinguishedName& dn)
{
    println("  {}:", label);
    if (!dn.raw_dn.empty())              println("    Raw DN : {}", dn.raw_dn);
    if (!dn.common_name.empty())         println("    CN     : {}", dn.common_name);
    if (!dn.organization.empty())        println("    O      : {}", dn.organization);
    if (!dn.organizational_unit.empty()) println("    OU     : {}", dn.organizational_unit);
    if (!dn.country.empty())             println("    C      : {}", dn.country);
    if (!dn.state.empty())               println("    ST     : {}", dn.state);
    if (!dn.locality.empty())            println("    L      : {}", dn.locality);
    if (!dn.email.empty())               println("    Email  : {}", dn.email);
}

[[nodiscard]] static std::string format_time(std::chrono::system_clock::time_point tp)
{
    auto tt = std::chrono::system_clock::to_time_t(tp);
    char buf[32]{};
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S UTC", std::gmtime(&tt));
    return buf;
}

static void print_info(const X509CertInfo& info)
{
    println("╔══════════════════════════════════════════════════════════╗");
    println("║               X.509 Certificate Information              ║");
    println("╚══════════════════════════════════════════════════════════╝");

    println("\n── Identity ─────────────────────────────────────────────────");
    println("  Version     : v{}", info.version);
    println("  Serial      : {}", info.serial_hex);
    println("  Self-signed : {}", info.self_signed ? "yes" : "no");
    print_dn("Subject", info.subject);
    print_dn("Issuer",  info.issuer);

    println("\n── Validity ─────────────────────────────────────────────────");
    println("  Not Before  : {}", format_time(info.not_before));
    println("  Not After   : {}", format_time(info.not_after));
    {
        auto now  = std::chrono::system_clock::now();
        auto days = std::chrono::duration_cast<std::chrono::days>(
                        info.not_after - now).count();
        if (days >= 0) println("  Remaining   : {} day(s)", days);
        else           println("  Remaining   : EXPIRED ({} day(s) ago)", -days);
    }

    println("\n── Subject Alternative Names ────────────────────────────────");
    for (auto& s : info.san_dns)   println("  DNS   : {}", s);
    for (auto& s : info.san_ip)    println("  IP    : {}", s);
    for (auto& s : info.san_email) println("  Email : {}", s);
    for (auto& s : info.san_uri)   println("  URI   : {}", s);
    if (info.san_dns.empty() && info.san_ip.empty() &&
        info.san_email.empty() && info.san_uri.empty())
        println("  (none)");

    println("\n── Public Key ───────────────────────────────────────────────");
    println("  Algorithm   : {}", info.public_key.algorithm);
    if (info.public_key.bits) println("  Bits        : {}", info.public_key.bits);
    if (!info.public_key.curve.empty()) println("  Curve       : {}", info.public_key.curve);

    println("\n── Signature ────────────────────────────────────────────────");
    println("  Algorithm   : {}", info.signature_algorithm);

    println("\n── Constraints & Usage ──────────────────────────────────────");
    println("  CA          : {}", info.is_ca ? "yes" : "no");
    if (info.is_ca && info.path_len_constraint >= 0)
        println("  Path Length : {}", info.path_len_constraint);
    if (info.key_usage)
        println("  Key Usage   : {}", join(key_usage_strings(info.key_usage), ", "));
    if (!info.extended_key_usage.empty())
        println("  Ext KU      : {}", join(info.extended_key_usage, ", "));

    println("\n── Fingerprints ─────────────────────────────────────────────");
    println("  SHA-1       : {}", info.fingerprint_sha1);
    println("  SHA-256     : {}", info.fingerprint_sha256);

    println("\n── Extensions ({}) ──────────────────────────────────────────",
            info.extensions.size());
    for (auto& ext : info.extensions) {
        println("  OID         : {}  critical={}", ext.oid, ext.critical ? "yes" : "no");
        if (!ext.value_hex.empty()) {
            auto preview = ext.value_hex.substr(
                0, std::min<std::size_t>(ext.value_hex.size(), 64));
            println("  Value (hex) : {}{}", preview,
                    ext.value_hex.size() > 64 ? "…" : "");
        }
    }
}

// ── Entry point ───────────────────────────────────────────────────────────────
int main(int argc, char* argv[])
{
    gnutls_global_init();

    std::string pem;
    if (argc > 1) {
        std::ifstream f(argv[1]);
        if (!f) {
            println(stderr, "error: cannot open '{}'", argv[1]);
            gnutls_global_deinit();
            return 1;
        }
        pem = { std::istreambuf_iterator<char>(f), {} };
    } else {
        std::fputs("Reading PEM from stdin …\n", stderr);
        pem = { std::istreambuf_iterator<char>(std::cin), {} };
    }

    auto result = parse_pem(pem);
    if (!result) {
        println(stderr, "Parse error: {}", result.error());
        gnutls_global_deinit();
        return 2;
    }

    print_info(*result);
    gnutls_global_deinit();
    return 0;
}
