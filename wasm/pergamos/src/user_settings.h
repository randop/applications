#ifndef USER_SETTINGS_H
#define USER_SETTINGS_H

#define WOLFSSL_USER_SETTINGS
#define WOLFCRYPT_ONLY

/* RNG */
#define HAVE_HASHDRBG
#define WC_RESEED_INTERVAL 1000000
#define CUSTOM_RAND_GENERATE_BLOCK myRngFunc

/* ECC */
#define HAVE_ECC
#define HAVE_ECC_KEY_EXPORT
#define HAVE_ECC_SIGN
#define HAVE_ECC_VERIFY
#define HAVE_ECC_DHE
#define HAVE_ECC256
#define ECC_USER_CURVES
#define ECC_MIN_KEY_SZ 256

#define WOLFSSL_SP_MATH_ALL
#define SP_WORD_SIZE 32
#define WOLFSSL_SP_SMALL

/* Cert gen */
#define WOLFSSL_CERT_GEN
#define WOLFSSL_CERT_REQ
#define WOLFSSL_KEY_GEN
#define WOLFSSL_DER_TO_PEM

/* Disable */
#define WOLFSSL_NO_SOCK
#define NO_WRITEV
#define WOLFSSL_USER_IO
#define NO_BASE64
#define NO_SESSION_CACHE
#define NO_RSA
#define NO_DH
#define NO_DSA
#define NO_MD4
#define NO_RC4
#define NO_HC128
#define NO_PSK
#define NO_PWDBASED
#define NO_AES_GCM
#define NO_DES3
#define NO_FILESYSTEM
#define WOLFSSL_SMALL_STACK

#ifndef WOLFSSL_SHA384
#define WOLFSSL_SHA384
#endif
#ifndef WOLFSSL_SHA512
#define WOLFSSL_SHA512
#endif


#endif
