#include "proto/pairing.h"
#include "net/http.h"
#include "common/crypto.h"
#include "common/hoststore.h"
#include "common/log.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* This implements the real GameStream pairing state machine, ported from
 * moonlight-embedded/libgamestream (client.c gs_pair). The host must be a
 * Sunshine/GeForce Experience instance speaking the GameStream HTTP pairing
 * protocol. Steps 1-4 run over plain HTTP on httpPort; step 5 (pairchallenge)
 * runs over HTTPS on httpsPort with the freshly generated client cert for
 * mutual TLS. */

#define UUID_HEX_LEN 32

static void bytes_to_hex(const unsigned char *in, char *out, size_t len)
{
    static const char *h = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i * 2]     = h[in[i] >> 4];
        out[i * 2 + 1] = h[in[i] & 0xf];
    }
    out[len * 2] = 0;
}

static int hex_to_bytes(const char *in, unsigned char *out, size_t hexlen)
{
    size_t n = hexlen / 2;
    for (size_t i = 0; i < n; i++) {
        unsigned int b = 0;
        if (sscanf(in + i * 2, "%2x", &b) != 1)
            return -1;
        out[i] = (unsigned char)b;
    }
    return 0;
}

/* GameStream pairing returns simple XML (<root><tag>value</tag>...</root>). */
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

/* Fetch serverinfo over plain HTTP to learn httpsPort + server major version.
 * Best-effort: defaults are applied if it fails. */
static void fetch_serverinfo(paired_host_t *host)
{
    if (host->httpPort == 0)
        host->httpPort = 47989;

    char uuid[UUID_HEX_LEN + 1];
    unsigned char ru[16];
    crypto_rand(ru, sizeof(ru));
    bytes_to_hex(ru, uuid, sizeof(ru));

    char url[512];
    snprintf(url, sizeof(url),
             "http://%s:%d/serverinfo?uniqueid=%s&uuid=%s",
             host->ip, host->httpPort, host->unique_id, uuid);

    http_response_t resp;
    if (http_get(url, host, &resp) != 0) {
        LOGW("pair: serverinfo request failed; using defaults\n");
        host->httpsPort = host->httpsPort ? host->httpsPort : 47984;
        host->serverMajorVersion = host->serverMajorVersion ? host->serverMajorVersion : 7;
        return;
    }
    char tmp[64];
    if (xml_get_text(resp.body, "HttpsPort", tmp, sizeof(tmp)) == 0 && atoi(tmp) > 0)
        host->httpsPort = atoi(tmp);
    else
        host->httpsPort = 47984;
    if (xml_get_text(resp.body, "appversion", tmp, sizeof(tmp)) == 0 && atoi(tmp) > 0)
        host->serverMajorVersion = atoi(tmp);
    else
        host->serverMajorVersion = 7;
    http_response_free(&resp);
    LOGI("pair: server httpsPort=%d version=%d\n", host->httpsPort, host->serverMajorVersion);
}

int pair_with_host(paired_host_t *host, const char *pin)
{
    if (crypto_init() != 0)
        return -1;

    /* Generate/load the client RSA keypair + self-signed cert. */
    if (host->cert_pem[0] == 0) {
        if (crypto_gen_keypair_and_cert(host->cert_pem, sizeof(host->cert_pem),
                                        host->key_pem, sizeof(host->key_pem)) != 0) {
            LOGE("pair: client cert generation failed\n");
            return -1;
        }
    }
    if (host->unique_id[0] == 0) {
        unsigned char ru[8];
        crypto_rand(ru, sizeof(ru));
        bytes_to_hex(ru, host->unique_id, sizeof(ru));
    }
    if (host->httpPort == 0)
        host->httpPort = 47989;

    fetch_serverinfo(host);

    char *url = malloc(16384);
    if (!url)
        return -1;

    /* ---- Step 1: getservercert (HTTP) ---- */
    unsigned char salt_data[16];
    crypto_rand(salt_data, sizeof(salt_data));
    char salt_hex[sizeof(salt_data) * 2 + 1];
    bytes_to_hex(salt_data, salt_hex, sizeof(salt_data));

    char cert_hex[HOST_CERT_LEN * 2 + 1];
    bytes_to_hex((const unsigned char *)host->cert_pem, cert_hex,
                 strlen(host->cert_pem));

    unsigned char ru[16];
    char uuid[UUID_HEX_LEN + 1];
    crypto_rand(ru, sizeof(ru));
    bytes_to_hex(ru, uuid, sizeof(ru));
    snprintf(url, 16384,
             "http://%s:%d/pair?uniqueid=%s&uuid=%s&devicename=roth&updateState=1&phrase=getservercert&salt=%s&clientcert=%s",
             host->ip, host->httpPort, host->unique_id, uuid, salt_hex, cert_hex);

    http_response_t resp;
    if (http_get(url, host, &resp) != 0) {
        LOGE("pair: step1 request failed\n");
        free(url);
        return -1;
    }
    char paired[8] = {0}, plaincert_hex[HOST_CERT_LEN * 2 + 1] = {0};
    xml_get_text(resp.body, "paired", paired, sizeof(paired));
    xml_get_text(resp.body, "plaincert", plaincert_hex, sizeof(plaincert_hex));
    http_response_free(&resp);
    if (strcmp(paired, "1") != 0) {
        LOGE("pair: step1 rejected (paired=%s)\n", paired);
        free(url);
        return -1;
    }
    /* plaincert is hex of the host's cert (PEM text). */
    unsigned char plaincert[HOST_CERT_LEN];
    size_t plaincert_len = strlen(plaincert_hex) / 2;
    if (plaincert_len >= sizeof(plaincert) ||
        hex_to_bytes(plaincert_hex, plaincert, strlen(plaincert_hex)) != 0) {
        LOGE("pair: bad plaincert\n");
        free(url);
        return -1;
    }
    plaincert[plaincert_len] = 0; /* ensure NUL for PEM parse */

    /* ---- Step 2: clientchallenge (HTTP) ---- */
    unsigned char salt_pin[sizeof(salt_data) + 4];
    memcpy(salt_pin, salt_data, sizeof(salt_data));
    memcpy(salt_pin + sizeof(salt_data), pin, 4);
    int hash_length = host->serverMajorVersion >= 7 ? 32 : 20;

    unsigned char aes_key[32];
    crypto_sha256(salt_pin, sizeof(salt_pin), aes_key); /* SHA-256 */

    unsigned char challenge_data[16];
    crypto_rand(challenge_data, sizeof(challenge_data));
    unsigned char challenge_enc[16];
    crypto_aes128_ecb(aes_key, challenge_data, sizeof(challenge_data),
                      challenge_enc, 1);
    char challenge_hex[sizeof(challenge_enc) * 2 + 1];
    bytes_to_hex(challenge_enc, challenge_hex, sizeof(challenge_enc));

    crypto_rand(ru, sizeof(ru));
    bytes_to_hex(ru, uuid, sizeof(ru));
    snprintf(url, 16384,
             "http://%s:%d/pair?uniqueid=%s&uuid=%s&devicename=roth&updateState=1&clientchallenge=%s",
             host->ip, host->httpPort, host->unique_id, uuid, challenge_hex);
    if (http_get(url, host, &resp) != 0) {
        LOGE("pair: step2 request failed\n");
        free(url);
        return -1;
    }
    char challengeresp_hex[128] = {0};
    memset(paired, 0, sizeof(paired));
    xml_get_text(resp.body, "paired", paired, sizeof(paired));
    xml_get_text(resp.body, "challengeresponse", challengeresp_hex, sizeof(challengeresp_hex));
    http_response_free(&resp);
    if (strcmp(paired, "1") != 0 || challengeresp_hex[0] == 0) {
        LOGE("pair: step2 rejected (paired=%s)\n", paired);
        free(url);
        return -1;
    }
    unsigned char challenge_response_data[64];
    size_t cr_len = strlen(challengeresp_hex) / 2;
    if (cr_len % 16 != 0 || cr_len > sizeof(challenge_response_data) ||
        hex_to_bytes(challengeresp_hex, challenge_response_data, strlen(challengeresp_hex)) != 0) {
        LOGE("pair: bad challengeresponse\n");
        free(url);
        return -1;
    }
    unsigned char challenge_response_plain[64];
    crypto_aes128_ecb(aes_key, challenge_response_data, cr_len,
                      challenge_response_plain, 0);

    /* ---- Step 3: serverchallengeresp (HTTP) ---- */
    unsigned char client_secret_data[16];
    crypto_rand(client_secret_data, sizeof(client_secret_data));

    unsigned char certsig[256];
    size_t certsig_len = 0;
    if (crypto_cert_signature(host->cert_pem, certsig, sizeof(certsig),
                              &certsig_len) != 0) {
        LOGE("pair: failed to read client cert signature\n");
        free(url);
        return -1;
    }

    size_t blob_len = 16 + certsig_len + sizeof(client_secret_data);
    unsigned char *challenge_response = malloc(blob_len);
    if (!challenge_response) { free(url); return -1; }
    memcpy(challenge_response, challenge_response_plain + hash_length, 16);
    memcpy(challenge_response + 16, certsig, certsig_len);
    memcpy(challenge_response + 16 + certsig_len, client_secret_data,
           sizeof(client_secret_data));

    unsigned char hash[32];
    crypto_sha256(challenge_response, blob_len, hash);
    unsigned char hash_enc[32];
    crypto_aes128_ecb(aes_key, hash, sizeof(hash), hash_enc, 1);
    char hash_hex[sizeof(hash_enc) * 2 + 1];
    bytes_to_hex(hash_enc, hash_hex, sizeof(hash_enc));
    free(challenge_response);

    crypto_rand(ru, sizeof(ru));
    bytes_to_hex(ru, uuid, sizeof(ru));
    snprintf(url, 16384,
             "http://%s:%d/pair?uniqueid=%s&uuid=%s&devicename=roth&updateState=1&serverchallengeresp=%s",
             host->ip, host->httpPort, host->unique_id, uuid, hash_hex);
    if (http_get(url, host, &resp) != 0) {
        LOGE("pair: step3 request failed\n");
        free(url);
        return -1;
    }
    char pairingsecret_hex[1024] = {0};
    memset(paired, 0, sizeof(paired));
    xml_get_text(resp.body, "paired", paired, sizeof(paired));
    xml_get_text(resp.body, "pairingsecret", pairingsecret_hex, sizeof(pairingsecret_hex));
    http_response_free(&resp);
    if (strcmp(paired, "1") != 0 || pairingsecret_hex[0] == 0) {
        LOGE("pair: step3 rejected (paired=%s)\n", paired);
        free(url);
        return -1;
    }
    unsigned char pairing_secret[512];
    size_t ps_len = strlen(pairingsecret_hex) / 2;
    if (ps_len <= 16 || ps_len > sizeof(pairing_secret) ||
        hex_to_bytes(pairingsecret_hex, pairing_secret, strlen(pairingsecret_hex)) != 0) {
        LOGE("pair: bad pairingsecret\n");
        free(url);
        return -1;
    }
    if (crypto_x509_verify((const char *)plaincert, pairing_secret, 16,
                           pairing_secret + 16, ps_len - 16) != 0) {
        LOGE("pair: pairingsecret signature verify failed (MITM?)\n");
        free(url);
        return -1;
    }

    /* ---- Step 4: clientpairingsecret (HTTP) ---- */
    unsigned char sig[512];
    size_t sig_len = 0;
    if (crypto_rsa_sign(host->key_pem, client_secret_data,
                        sizeof(client_secret_data), sig, &sig_len) != 0) {
        LOGE("pair: failed to sign client secret\n");
        free(url);
        return -1;
    }
    unsigned char client_pairing_secret[512];
    size_t cps_len = sizeof(client_secret_data) + sig_len;
    memcpy(client_pairing_secret, client_secret_data, sizeof(client_secret_data));
    memcpy(client_pairing_secret + sizeof(client_secret_data), sig, sig_len);
    char cps_hex[1024];
    bytes_to_hex(client_pairing_secret, cps_hex, cps_len);

    crypto_rand(ru, sizeof(ru));
    bytes_to_hex(ru, uuid, sizeof(ru));
    snprintf(url, 16384,
             "http://%s:%d/pair?uniqueid=%s&uuid=%s&devicename=roth&updateState=1&clientpairingsecret=%s",
             host->ip, host->httpPort, host->unique_id, uuid, cps_hex);
    if (http_get(url, host, &resp) != 0) {
        LOGE("pair: step4 request failed\n");
        free(url);
        return -1;
    }
    memset(paired, 0, sizeof(paired));
    xml_get_text(resp.body, "paired", paired, sizeof(paired));
    http_response_free(&resp);
    if (strcmp(paired, "1") != 0) {
        LOGE("pair: step4 rejected (paired=%s)\n", paired);
        free(url);
        return -1;
    }

    /* ---- Step 5: pairchallenge (HTTPS, mutual TLS) ---- */
    crypto_rand(ru, sizeof(ru));
    bytes_to_hex(ru, uuid, sizeof(ru));
    snprintf(url, 16384,
             "https://%s:%d/pair?uniqueid=%s&uuid=%s&devicename=roth&updateState=1&phrase=pairchallenge",
             host->ip, host->httpsPort, host->unique_id, uuid);
    if (http_get(url, host, &resp) != 0) {
        LOGE("pair: step5 request failed\n");
        free(url);
        return -1;
    }
    memset(paired, 0, sizeof(paired));
    xml_get_text(resp.body, "paired", paired, sizeof(paired));
    http_response_free(&resp);
    if (strcmp(paired, "1") != 0) {
        LOGE("pair: step5 rejected (paired=%s)\n", paired);
        free(url);
        return -1;
    }

    host->paired = 1;
    hoststore_save();
    LOGI("pair: successfully paired with %s\n", host->ip);
    free(url);
    return 0;
}
