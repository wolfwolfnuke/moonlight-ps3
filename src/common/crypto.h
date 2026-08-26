#ifndef CRYPTO_H
#define CRYPTO_H

#include <stddef.h>
#include <mbedtls/ctr_drbg.h>

/* Thin wrapper over mbedTLS used for pairing (RSA/DH/AES) and HTTPS,
 * plus a CSPRNG for nonces/challenges. */

int crypto_init(void);
void crypto_rand(void *buf, size_t len);

/* Expose the global CTR-DRBG for use as the RNG in TLS contexts. */
mbedtls_ctr_drbg_context *crypto_get_drbg(void);

/* Generate a 2048-bit RSA keypair and a self-signed X.509 client certificate
 * (subject CN=PS3, O=MoonlightPS3), writing both as PEM strings.
 * Returns 0 on success. Caller must free nothing (buffers are caller-owned). */
int crypto_gen_keypair_and_cert(char *cert_pem, size_t cert_cap,
                                char *key_pem, size_t key_cap);

/* SHA-256 of data -> 32-byte digest. */
void crypto_sha256(const void *data, size_t len, uint8_t out[32]);

/* Derive the 16-byte AES session key/iv from a 32-byte digest
 * (key = digest[0:16], iv = digest[16:32]), per GameStream pairing. */
void crypto_derive_keyiv(const uint8_t digest[32], uint8_t key[16], uint8_t iv[16]);

/* RSA-encrypt `in` with the public key parsed from a PEM certificate
 * (the GameStream host cert / "challenge"). Output length in *out_len.
 * Returns 0 on success. */
int crypto_rsa_encrypt_pubkey_pem(const char *cert_pem,
                                  const void *in, size_t in_len,
                                  uint8_t *out, size_t *out_len);

#endif /* CRYPTO_H */
