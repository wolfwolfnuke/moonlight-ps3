#include "proto/pairing.h"
#include "net/http.h"
#include "common/crypto.h"
#include "common/hoststore.h"
#include "common/log.h"

#include <mbedtls/base64.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* GameStream pairing returns simple XML (<root><tag>value</tag>...</root>).
 * A full parser (libxml2) can replace this; for now a tiny tag extractor is
 * sufficient and avoids pulling libxml2 into the critical path. TODO: handle
 * attributes/CDATA if a host ever emits them. */
static int xml_get_text(const char *body, const char *tag,
                        char *out, size_t outcap)
{
    char open[64];
    snprintf(open, sizeof(open), "<%s>", tag);
    const char *s = strstr(body, open);
    if (!s)
        return -1;
    s += strlen(open);
    const char *e = strstr(s, "</");
    if (!e)
        return -1;
    size_t n = (size_t)(e - s);
    if (n >= outcap)
        n = outcap - 1;
    memcpy(out, s, n);
    out[n] = 0;
    return 0;
}

int pair_with_host(paired_host_t *host, const char *pin)
{
    if (crypto_init() != 0)
        return -1;

    /* 1. Generate the client RSA keypair + self-signed cert. */
    if (crypto_gen_keypair_and_cert(host->cert_pem, sizeof(host->cert_pem),
                                    host->key_pem, sizeof(host->key_pem)) != 0) {
        LOGE("pair: client cert generation failed\n");
        return -1;
    }

    char url[128];
    snprintf(url, sizeof(url), "https://%s:47989/pair", host->ip);

    /* 2. Phase 1: announce client cert, get PIN (phrase), salt, server cert. */
    char body1[4096];
    snprintf(body1, sizeof(body1),
        "<root><deviceName>PS3</deviceName><updateState>1</updateState>"
        "<devicetype>1</devicetype>"
        "<clientcert>%s</clientcert></root>", host->cert_pem);

    http_response_t resp;
    if (http_post(url, host, body1, &resp) != 0) {
        LOGE("pair: phase 1 request failed\n");
        return -1;
    }

    char paired[8] = {0}, salt[256] = {0}, phrase[64] = {0};
    char challenge[HOST_CERT_LEN] = {0};
    xml_get_text(resp.body, "paired", paired, sizeof(paired));
    xml_get_text(resp.body, "salt", salt, sizeof(salt));
    xml_get_text(resp.body, "phrase", phrase, sizeof(phrase));
    xml_get_text(resp.body, "challenge", challenge, sizeof(challenge));
    http_response_free(&resp);

    if (strcmp(paired, "1") == 0) {
        host->paired = 1;
        hoststore_save();
        LOGI("pair: already paired with %s\n", host->ip);
        return 0;
    }

    LOGI("pair: enter PIN '%s' on the host, then confirm\n", phrase);

    /* 3. pinHash = SHA-256(salt || pin); first 16 bytes = key, next 16 = iv. */
    char concat[512];
    int cl = snprintf(concat, sizeof(concat), "%s%s", salt, pin);
    uint8_t pin_hash[32];
    crypto_sha256(concat, (size_t)cl, pin_hash);
    crypto_derive_keyiv(pin_hash, host->key, host->iv);

    /* 4. Encrypt pinHash with the host's public key (from its challenge cert). */
    uint8_t enc[512];
    size_t enclen = sizeof(enc);
    if (crypto_rsa_encrypt_pubkey_pem(challenge, pin_hash, sizeof(pin_hash),
                                      enc, &enclen) != 0) {
        LOGE("pair: failed to encrypt pin hash\n");
        return -1;
    }
    char b64[1024];
    size_t b64len = 0;
    if (mbedtls_base64_encode((unsigned char *)b64, sizeof(b64), &b64len,
                              enc, enclen) != 0) {
        LOGE("pair: base64 encode failed\n");
        return -1;
    }

    /* 5. Phase 2: send the encrypted pin hash back for the host to verify.
     * TODO: verify the exact field names / updateState value against
     * moonlight-common-c / Sunshine — the wire contract is host-specific. */
    char body2[4096];
    snprintf(body2, sizeof(body2),
        "<root><deviceName>PS3</deviceName><updateState>1</updateState>"
        "<devicetype>1</devicetype>"
        "<clientcert>%s</clientcert>"
        "<encryptedpinhash>%s</encryptedpinhash></root>",
        host->cert_pem, b64);

    if (http_post(url, host, body2, &resp) != 0) {
        LOGE("pair: phase 2 request failed\n");
        return -1;
    }
    memset(paired, 0, sizeof(paired));
    xml_get_text(resp.body, "paired", paired, sizeof(paired));
    http_response_free(&resp);

    if (strcmp(paired, "1") == 0) {
        host->paired = 1;
        hoststore_save();
        LOGI("pair: successfully paired with %s\n", host->ip);
        return 0;
    }

    LOGE("pair: host did not confirm pairing (paired=%s)\n", paired);
    return -1;
}
