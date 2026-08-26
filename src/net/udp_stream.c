#include "net/udp_stream.h"
#include "sock.h"
#include "common/log.h"
#include "common/crypto.h"

#include <mbedtls/cipher.h>

#include <string.h>
#include <stdio.h>

int udp_open(udp_stream_t *u, const rtsp_session_t *s)
{
    memset(u, 0, sizeof(*u));
    u->video_fd   = net_udp_socket();
    u->audio_fd   = net_udp_socket();
    u->control_fd = net_udp_socket();
    if (u->video_fd < 0 || u->audio_fd < 0 || u->control_fd < 0)
        return -1;
    if (net_bind(u->video_fd,   s->video_local_port)   < 0 ||
        net_bind(u->audio_fd,   s->audio_local_port)   < 0 ||
        net_bind(u->control_fd, s->control_local_port) < 0) {
        LOGE("udp: failed to bind stream sockets\n");
        return -1;
    }
    return 0;
}

void udp_close(udp_stream_t *u)
{
    if (u->video_fd   >= 0) net_close(u->video_fd);
    if (u->audio_fd   >= 0) net_close(u->audio_fd);
    if (u->control_fd >= 0) net_close(u->control_fd);
    u->video_fd = u->audio_fd = u->control_fd = -1;
}

int udp_recv(udp_stream_t *u, int which, uint8_t *buf, size_t cap, int ms)
{
    int fd = which == UDP_VIDEO ? u->video_fd
           : which == UDP_AUDIO ? u->audio_fd
                                : u->control_fd;
    if (fd < 0)
        return -1;
    return net_recvfrom(fd, buf, (int)cap, ms);
}

int control_decrypt(const uint8_t *enc, size_t elen,
                    uint8_t *dec, size_t *dlen,
                    const uint8_t key[16], const uint8_t iv[16])
{
    mbedtls_cipher_context_t ctx;
    mbedtls_cipher_init(&ctx);

    const mbedtls_cipher_info_t *info =
        mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_128_CBC);
    if (!info || mbedtls_cipher_setup(&ctx, info) != 0) {
        LOGE("udp: cipher setup failed\n");
        mbedtls_cipher_free(&ctx);
        return -1;
    }
    if (mbedtls_cipher_setkey(&ctx, key, 128, MBEDTLS_DECRYPT) != 0 ||
        mbedtls_cipher_set_iv(&ctx, iv, 16) != 0 ||
        mbedtls_cipher_update(&ctx, enc, elen, dec, dlen) != 0 ||
        mbedtls_cipher_finish(&ctx, dec + *dlen, dlen) != 0) {
        LOGE("udp: aes decrypt failed\n");
        mbedtls_cipher_free(&ctx);
        return -1;
    }
    /* Strip PKCS#7 padding. */
    size_t total = *dlen;
    if (total == 0) {
        mbedtls_cipher_free(&ctx);
        return -1;
    }
    int pad = dec[total - 1];
    if (pad < 1 || pad > 16 || (size_t)pad > total) {
        mbedtls_cipher_free(&ctx);
        return -1;
    }
    total -= (size_t)pad;
    *dlen = total;
    mbedtls_cipher_free(&ctx);
    return 0;
}

int control_encrypt(uint8_t *out, size_t outcap,
                     const uint8_t *plain, size_t plen,
                     const uint8_t key[16])
{
    if (plen == 0 || outcap < 16)
        return -1;
    size_t pad = 16 - (plen % 16);
    size_t padded = plen + pad;
    if (outcap < 16 + padded || padded > 1024)
        return -1;

    uint8_t buf[1024];
    memcpy(buf, plain, plen);
    for (size_t i = 0; i < pad; i++)
        buf[plen + i] = (uint8_t)pad;

    uint8_t iv[16];
    crypto_rand(iv, 16);
    memcpy(out, iv, 16);

    mbedtls_cipher_context_t ctx;
    mbedtls_cipher_init(&ctx);
    const mbedtls_cipher_info_t *info =
        mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_128_CBC);
    if (!info || mbedtls_cipher_setup(&ctx, info) != 0 ||
        mbedtls_cipher_setkey(&ctx, key, 128, MBEDTLS_ENCRYPT) != 0 ||
        mbedtls_cipher_set_iv(&ctx, iv, 16) != 0) {
        LOGE("udp: cipher setup failed\n");
        mbedtls_cipher_free(&ctx);
        return -1;
    }
    size_t olen = 0, total = 0;
    if (mbedtls_cipher_update(&ctx, buf, padded, out + 16, &olen) != 0 ||
        mbedtls_cipher_finish(&ctx, out + 16 + olen, &olen) != 0) {
        LOGE("udp: aes encrypt failed\n");
        mbedtls_cipher_free(&ctx);
        return -1;
    }
    total += olen;
    mbedtls_cipher_free(&ctx);
    return (int)(16 + total);
}

int control_keepalive(int control_fd, const char *host, int control_port)
{
    /* TODO: build and AES-encrypt a real RI control message. For now send a
     * single marker byte so the socket stays open (placeholder only). */
    uint8_t ping = 0x01;
    if (net_sendto(control_fd, &ping, 1, host, control_port) < 0) {
        LOGW("udp: keepalive send failed\n");
        return -1;
    }
    return 0;
}
