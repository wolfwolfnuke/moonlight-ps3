#include "crypto.h"
#include "log.h"

#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/sha256.h>
#include <mbedtls/aes.h>
#include <mbedtls/platform_time.h>

#include <string.h>
#include <time.h>

static mbedtls_entropy_context g_entropy;
static mbedtls_ctr_drbg_context g_drbg;
static int g_inited = 0;

/* The bare PS3 has no OS entropy source. This is a PLACEHOLDER PRNG — it is
 * NOT cryptographically secure and MUST be replaced with a real entropy source
 * (a PS3 hardware RNG syscall, network packet timing, etc.) before this client
 * is used for real pairing. The seed is at least time-varying via clock(). */
static int ml_entropy(void *p, unsigned char *out, size_t len, size_t *olen)
{
    (void)p;
    static unsigned long seed = 0;
    if (seed == 0)
        seed = (unsigned long)clock() ^ 0x9e3779b9u;
    for (size_t i = 0; i < len; i++) {
        seed ^= seed << 13;
        seed ^= seed >> 7;
        seed ^= seed << 17;
        out[i] = (unsigned char)(seed & 0xff);
    }
    *olen = len;
    return 0;
}

/* Provided because mbedTLS is built with MBEDTLS_PLATFORM_MS_TIME_ALT.
 * Returns a monotonic placeholder timestamp in milliseconds. */
#if defined(MBEDTLS_PLATFORM_MS_TIME_ALT)
mbedtls_ms_time_t mbedtls_ms_time(void)
{
    static mbedtls_ms_time_t t = 0;
    return ++t;
}
#endif

int crypto_init(void)
{
    if (g_inited)
        return 0;

    mbedtls_entropy_init(&g_entropy);
    mbedtls_ctr_drbg_init(&g_drbg);

    mbedtls_entropy_add_source(&g_entropy, ml_entropy, NULL, 128,
                               MBEDTLS_ENTROPY_SOURCE_STRONG);

    if (mbedtls_ctr_drbg_seed(&g_drbg, mbedtls_entropy_func, &g_entropy,
                              NULL, 0) != 0) {
        LOGE("crypto: ctr_drbg seed failed\n");
        return -1;
    }

    g_inited = 1;
    return 0;
}

void crypto_rand(void *buf, size_t len)
{
    if (!g_inited)
        crypto_init();
    mbedtls_ctr_drbg_random(&g_drbg, (unsigned char *)buf, len);
}

mbedtls_ctr_drbg_context *crypto_get_drbg(void)
{
    if (!g_inited)
        crypto_init();
    return &g_drbg;
}

int crypto_gen_keypair_and_cert(char *cert_pem, size_t cert_cap,
                                char *key_pem, size_t key_cap)
{
    mbedtls_pk_context pk;
    mbedtls_x509write_cert crt;
    int ret = -1;

    mbedtls_pk_init(&pk);
    mbedtls_x509write_crt_init(&crt);

    if (mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA)) != 0)
        goto out;
    if (mbedtls_rsa_gen_key(mbedtls_pk_rsa(pk), mbedtls_ctr_drbg_random,
                            crypto_get_drbg(), 2048, 65537) != 0) {
        LOGE("crypto: rsa keygen failed\n");
        goto out;
    }

    const char *subj = "C=US,ST=CA,L=,O=MoonlightPS3,OU=,CN=PS3";
    mbedtls_x509write_crt_set_version(&crt, 2); /* X.509 v3 */
    mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&crt, &pk);
    mbedtls_x509write_crt_set_issuer_key(&crt, &pk);
    mbedtls_x509write_crt_set_subject_name(&crt, subj);
    mbedtls_x509write_crt_set_issuer_name(&crt, subj);
    mbedtls_x509write_crt_set_validity(&crt, "20200101000000", "20350101000000");

    if (mbedtls_x509write_crt_pem(&crt, (unsigned char *)cert_pem, cert_cap,
                                  mbedtls_ctr_drbg_random, crypto_get_drbg()) != 0) {
        LOGE("crypto: cert write failed\n");
        goto out;
    }
    if (mbedtls_pk_write_key_pem(&pk, (unsigned char *)key_pem, key_cap) != 0) {
        LOGE("crypto: key write failed\n");
        goto out;
    }
    ret = 0;

out:
    mbedtls_x509write_crt_free(&crt);
    mbedtls_pk_free(&pk);
    return ret;
}

void crypto_sha256(const void *data, size_t len, uint8_t out[32])
{
    mbedtls_sha256((const unsigned char *)data, len, out, 0);
}

void crypto_derive_keyiv(const uint8_t digest[32], uint8_t key[16], uint8_t iv[16])
{
    memcpy(key, digest, 16);
    memcpy(iv, digest + 16, 16);
}

int crypto_rsa_encrypt_pubkey_pem(const char *cert_pem,
                                  const void *in, size_t in_len,
                                  uint8_t *out, size_t *out_len)
{
    mbedtls_x509_crt cert;
    mbedtls_pk_context *pk;
    int ret = -1;

    mbedtls_x509_crt_init(&cert);
    if (mbedtls_x509_crt_parse(&cert, (const unsigned char *)cert_pem,
                               strlen(cert_pem) + 1) != 0) {
        LOGE("crypto: failed to parse host cert\n");
        return -1;
    }
    pk = &cert.pk;
    if (!mbedtls_pk_can_do(pk, MBEDTLS_PK_RSA)) {
        LOGE("crypto: host cert has no RSA key\n");
        goto out;
    }
    /* GameStream pairing uses PKCS#1 v1.5; force it so encryption is
     * deterministic regardless of the parser's default padding. */
    mbedtls_rsa_set_padding(mbedtls_pk_rsa(*pk),
                            MBEDTLS_RSA_PKCS_V15, MBEDTLS_MD_NONE);
    /* mbedTLS PK encrypt handles OAEP/legacy padding per its config */
    if (mbedtls_pk_encrypt(pk, (const unsigned char *)in, in_len,
                           out, out_len, *out_len,
                           mbedtls_ctr_drbg_random, crypto_get_drbg()) != 0) {
        LOGE("crypto: rsa encrypt failed\n");
        goto out;
    }
    ret = 0;
out:
    mbedtls_x509_crt_free(&cert);
    return ret;
}

int crypto_aes128_ecb(const uint8_t *key, const uint8_t *in, size_t len,
                      uint8_t *out, int encrypt)
{
    if (len % 16 != 0)
        return -1;
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    int rc = encrypt ? mbedtls_aes_setkey_enc(&aes, key, 128)
                     : mbedtls_aes_setkey_dec(&aes, key, 128);
    if (rc != 0) {
        mbedtls_aes_free(&aes);
        return -1;
    }
    for (size_t i = 0; i < len; i += 16) {
        if (mbedtls_aes_crypt_ecb(&aes,
                encrypt ? MBEDTLS_AES_ENCRYPT : MBEDTLS_AES_DECRYPT,
                in + i, out + i) != 0) {
            mbedtls_aes_free(&aes);
            return -1;
        }
    }
    mbedtls_aes_free(&aes);
    return 0;
}

int crypto_x509_verify(const char *cert_pem,
                       const void *data, size_t dlen,
                       const void *sig, size_t slen)
{
    mbedtls_x509_crt cert;
    mbedtls_x509_crt_init(&cert);
    int ret = -1;
    if (mbedtls_x509_crt_parse(&cert, (const unsigned char *)cert_pem,
                               strlen(cert_pem) + 1) != 0) {
        LOGE("crypto: verify: cert parse failed\n");
        goto out;
    }
    uint8_t hash[32];
    mbedtls_sha256((const unsigned char *)data, dlen, hash, 0);
    if (mbedtls_pk_verify(&cert.pk, MBEDTLS_MD_SHA256, hash, sizeof(hash),
                          (const unsigned char *)sig, slen) == 0)
        ret = 0;
    else
        LOGE("crypto: signature verification failed\n");
out:
    mbedtls_x509_crt_free(&cert);
    return ret;
}

int crypto_rsa_sign(const char *key_pem,
                    const void *data, size_t dlen,
                    uint8_t *sig, size_t *sig_len)
{
    mbedtls_pk_context key;
    mbedtls_pk_init(&key);
    int ret = -1;
    if (mbedtls_pk_parse_key(&key, (const unsigned char *)key_pem,
                             strlen(key_pem) + 1, NULL, 0) != 0) {
        LOGE("crypto: sign: key parse failed\n");
        goto out;
    }
    uint8_t hash[32];
    mbedtls_sha256((const unsigned char *)data, dlen, hash, 0);
    if (mbedtls_pk_sign(&key, MBEDTLS_MD_SHA256, hash, sizeof(hash),
                        sig, sig_len, mbedtls_ctr_drbg_random,
                        crypto_get_drbg()) == 0)
        ret = 0;
    else
        LOGE("crypto: signing failed\n");
out:
    mbedtls_pk_free(&key);
    return ret;
}

int crypto_cert_signature(const char *cert_pem,
                          uint8_t *out, size_t outcap, size_t *outlen)
{
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);
    int ret = -1;
    if (mbedtls_x509_crt_parse(&crt, (const unsigned char *)cert_pem,
                               strlen(cert_pem) + 1) != 0) {
        LOGE("crypto: cert signature: parse failed\n");
        goto out;
    }
    if (crt.sig.len > outcap) {
        LOGE("crypto: cert signature too large\n");
        goto out;
    }
    memcpy(out, crt.sig.p, crt.sig.len);
    *outlen = crt.sig.len;
    ret = 0;
out:
    mbedtls_x509_crt_free(&crt);
    return ret;
}
