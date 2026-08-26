#include "common/crypto.h"

#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>
#include <mbedtls/error.h>

#include <stdio.h>
#include <string.h>

static int parse_cert_and_key(const char *cert_pem, const char *key_pem)
{
    mbedtls_x509_crt cert;
    mbedtls_pk_context key;
    mbedtls_x509_crt_init(&cert);
    mbedtls_pk_init(&key);

    int r = mbedtls_x509_crt_parse(&cert,
                (const unsigned char *)cert_pem, strlen(cert_pem) + 1);
    if (r != 0) { printf("  cert parse failed: %d\n", r); return -1; }
    r = mbedtls_pk_parse_key(&key,
                (const unsigned char *)key_pem, strlen(key_pem) + 1, NULL, 0);
    if (r != 0) { printf("  key parse failed: %d\n", r); return -1; }
    if (!mbedtls_pk_can_do(&key, MBEDTLS_PK_RSA)) {
        printf("  not an RSA key\n"); return -1;
    }
    mbedtls_x509_crt_free(&cert);
    mbedtls_pk_free(&key);
    return 0;
}

int main(void)
{
    int fails = 0;

    /* 1. Generate client cert + key, verify they parse. */
    char cert[2048], key[2048];
    if (crypto_gen_keypair_and_cert(cert, sizeof(cert), key, sizeof(key)) != 0) {
        printf("FAIL: crypto_gen_keypair_and_cert\n"); fails++;
    } else if (parse_cert_and_key(cert, key) != 0) {
        printf("FAIL: generated cert/key did not parse\n"); fails++;
    } else {
        printf("ok:   keypair + self-signed cert generated and parseable\n");
    }

    /* 2. SHA-256 known-answer test: sha256("abc"). */
    static const uint8_t expect[32] = {
        0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad };
    uint8_t d[32];
    crypto_sha256("abc", 3, d);
    if (memcmp(d, expect, 32) != 0) {
        printf("FAIL: sha256 known-answer\n"); fails++;
    } else {
        printf("ok:   sha256(\"abc\") known-answer\n");
    }

    /* 3. key/iv derivation. */
    uint8_t k[16], iv[16];
    crypto_derive_keyiv(d, k, iv);
    if (memcmp(k, d, 16) != 0 || memcmp(iv, d + 16, 16) != 0) {
        printf("FAIL: derive_keyiv\n"); fails++;
    } else {
        printf("ok:   derive_keyiv (key=hash[0:16], iv=hash[16:32])\n");
    }

    /* 4. RSA round-trip: encrypt with cert public key, decrypt with private key. */
    uint8_t pin[32];
    crypto_sha256("salt12345PIN", 12, pin);
    uint8_t enc[512];
    size_t enclen = sizeof(enc);
    if (crypto_rsa_encrypt_pubkey_pem(cert, pin, sizeof(pin), enc, &enclen) != 0) {
        printf("FAIL: rsa encrypt (pubkey from cert)\n"); fails++;
    } else {
        mbedtls_pk_context pk;
        mbedtls_pk_init(&pk);
        if (mbedtls_pk_parse_key(&pk,
                (const unsigned char *)key, strlen(key) + 1, NULL, 0) != 0) {
            printf("FAIL: parse private key for decrypt\n"); fails++;
        } else {
            uint8_t dec[512];
            size_t declen = sizeof(dec);
            int dr = mbedtls_pk_decrypt(&pk, enc, enclen, dec, &declen,
                                        sizeof(dec), NULL, NULL);
            if (dr != 0) {
                char errbuf[128];
                mbedtls_strerror(dr, errbuf, sizeof(errbuf));
                printf("FAIL: rsa decrypt round-trip (dr=%d %s)\n", dr, errbuf); fails++;
            } else if (declen != 32 || memcmp(dec, pin, 32) != 0) {
                printf("FAIL: rsa decrypt round-trip (len/compare)\n"); fails++;
            } else {
                printf("ok:   rsa encrypt(pub from cert)/decrypt(priv) round-trip\n");
            }
        }
        mbedtls_pk_free(&pk);
    }

    if (fails == 0)
        printf("\nALL CRYPTO TESTS PASSED\n");
    else
        printf("\n%d CRYPTO TEST(S) FAILED\n", fails);
    return fails;
}
