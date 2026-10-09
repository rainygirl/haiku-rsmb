/* R SMB: Kerberos login to a Mac's Local KDC (LKDC) without GSSAPI.
 *
 * macOS only accepts NTLM for accounts with "Windows File Sharing" on. Every
 * other account logs in with Kerberos against the Mac's own KDC (TCP 88), in
 * a realm named LKDC:SHA1.<hash>. This file does exactly that exchange:
 * realm referral (WELLKNOWN:COM.APPLE.LKDC), AS-REQ with PA-ENC-TIMESTAMP,
 * TGS-REQ for cifs/<realm>, then an SPNEGO-wrapped AP-REQ for SMB session
 * setup. Only aes256/aes128-cts-hmac-sha1-96 (RFC 3962) are supported.
 * LGPL 2.1, like the rest of libsmb2.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "compat.h"
#include "smb2.h"
#include "libsmb2.h"
#include "libsmb2-private.h"
#include "rsmb-krb5.h"

#define LKDC_WELLKNOWN "WELLKNOWN:COM.APPLE.LKDC"
#define KDC_TIMEOUT_MS 5000

/* ---------- AES (FIPS-197), 128 and 256-bit keys ---------- */

static const uint8_t sbox[256] = {
        0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
        0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
        0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
        0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
        0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
        0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
        0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
        0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
        0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
        0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
        0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
        0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
        0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
        0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
        0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
        0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16 };

struct aes {
        int rounds;
        uint8_t rk[240];
};

static uint8_t inv_sbox[256];

static uint8_t xt(uint8_t x)
{
        return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1b : 0));
}

static uint8_t mul(uint8_t a, uint8_t b)
{
        uint8_t r = 0;
        while (b) {
                if (b & 1)
                        r ^= a;
                a = xt(a);
                b >>= 1;
        }
        return r;
}

static void aes_init(struct aes *a, const uint8_t *key, int len)
{
        int nk = len / 4, i;
        uint8_t rcon = 1;

        if (!inv_sbox[0x63] && !inv_sbox[0]) {
                for (i = 0; i < 256; i++)
                        inv_sbox[sbox[i]] = (uint8_t)i;
        }
        a->rounds = nk + 6;
        memcpy(a->rk, key, len);
        for (i = nk; i < 4 * (a->rounds + 1); i++) {
                uint8_t t[4];
                memcpy(t, a->rk + 4 * (i - 1), 4);
                if (i % nk == 0) {
                        uint8_t u = t[0];
                        t[0] = sbox[t[1]] ^ rcon;
                        t[1] = sbox[t[2]];
                        t[2] = sbox[t[3]];
                        t[3] = sbox[u];
                        rcon = xt(rcon);
                } else if (nk > 6 && i % nk == 4) {
                        int j;
                        for (j = 0; j < 4; j++)
                                t[j] = sbox[t[j]];
                }
                {
                        int j;
                        for (j = 0; j < 4; j++)
                                a->rk[4 * i + j] = a->rk[4 * (i - nk) + j] ^ t[j];
                }
        }
}

static void aes_encrypt(const struct aes *a, const uint8_t *in, uint8_t *out)
{
        uint8_t s[16], t[16];
        int r, i, c;

        for (i = 0; i < 16; i++)
                s[i] = in[i] ^ a->rk[i];
        for (r = 1; r <= a->rounds; r++) {
                for (i = 0; i < 16; i++) /* SubBytes + ShiftRows */
                        t[i] = sbox[s[(i + 4 * (i % 4)) % 16]];
                if (r != a->rounds) {
                        for (c = 0; c < 4; c++) {
                                uint8_t *p = t + 4 * c, x0 = p[0], x1 = p[1], x2 = p[2], x3 = p[3];
                                s[4 * c] = xt(x0) ^ xt(x1) ^ x1 ^ x2 ^ x3;
                                s[4 * c + 1] = x0 ^ xt(x1) ^ xt(x2) ^ x2 ^ x3;
                                s[4 * c + 2] = x0 ^ x1 ^ xt(x2) ^ xt(x3) ^ x3;
                                s[4 * c + 3] = xt(x0) ^ x0 ^ x1 ^ x2 ^ xt(x3);
                        }
                } else
                        memcpy(s, t, 16);
                for (i = 0; i < 16; i++)
                        s[i] ^= a->rk[16 * r + i];
        }
        memcpy(out, s, 16);
}

static void aes_decrypt(const struct aes *a, const uint8_t *in, uint8_t *out)
{
        uint8_t s[16], t[16];
        int r, i, c;

        for (i = 0; i < 16; i++)
                s[i] = in[i] ^ a->rk[16 * a->rounds + i];
        for (r = a->rounds - 1; r >= 0; r--) {
                for (i = 0; i < 16; i++) /* InvShiftRows + InvSubBytes */
                        t[(i + 4 * (i % 4)) % 16] = inv_sbox[s[i]];
                for (i = 0; i < 16; i++)
                        t[i] ^= a->rk[16 * r + i];
                if (r != 0) {
                        for (c = 0; c < 4; c++) {
                                uint8_t *p = t + 4 * c;
                                s[4 * c] = mul(p[0], 14) ^ mul(p[1], 11) ^ mul(p[2], 13) ^ mul(p[3], 9);
                                s[4 * c + 1] = mul(p[0], 9) ^ mul(p[1], 14) ^ mul(p[2], 11) ^ mul(p[3], 13);
                                s[4 * c + 2] = mul(p[0], 13) ^ mul(p[1], 9) ^ mul(p[2], 14) ^ mul(p[3], 11);
                                s[4 * c + 3] = mul(p[0], 11) ^ mul(p[1], 13) ^ mul(p[2], 9) ^ mul(p[3], 14);
                        }
                } else
                        memcpy(s, t, 16);
        }
        memcpy(out, s, 16);
}

/* ---------- RFC 3961/3962 ---------- */

/* SHA-1 (FIPS 180-4); libsmb2 builds its RFC 6234 copy without SHA-1. */
struct sha1 {
        uint32_t h[5];
        uint64_t len;
        uint8_t block[64];
        size_t used;
};

static uint32_t rol(uint32_t x, int n)
{
        return (x << n) | (x >> (32 - n));
}

static void sha1_block(struct sha1 *c, const uint8_t *p)
{
        uint32_t w[80], a, b, d, e, f, k, t, cc;
        int i;
        for (i = 0; i < 16; i++)
                w[i] = ((uint32_t)p[4 * i] << 24) | ((uint32_t)p[4 * i + 1] << 16) | ((uint32_t)p[4 * i + 2] << 8) | p[4 * i + 3];
        for (; i < 80; i++)
                w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        a = c->h[0];
        b = c->h[1];
        cc = c->h[2];
        d = c->h[3];
        e = c->h[4];
        for (i = 0; i < 80; i++) {
                if (i < 20) {
                        f = (b & cc) | (~b & d);
                        k = 0x5a827999;
                } else if (i < 40) {
                        f = b ^ cc ^ d;
                        k = 0x6ed9eba1;
                } else if (i < 60) {
                        f = (b & cc) | (b & d) | (cc & d);
                        k = 0x8f1bbcdc;
                } else {
                        f = b ^ cc ^ d;
                        k = 0xca62c1d6;
                }
                t = rol(a, 5) + f + e + k + w[i];
                e = d;
                d = cc;
                cc = rol(b, 30);
                b = a;
                a = t;
        }
        c->h[0] += a;
        c->h[1] += b;
        c->h[2] += cc;
        c->h[3] += d;
        c->h[4] += e;
}

static void sha1_init(struct sha1 *c)
{
        static const uint32_t h[5] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0 };
        memcpy(c->h, h, sizeof(h));
        c->len = 0;
        c->used = 0;
}

static void sha1_update(struct sha1 *c, const uint8_t *p, size_t n)
{
        c->len += n;
        while (n) {
                size_t take = 64 - c->used < n ? 64 - c->used : n;
                memcpy(c->block + c->used, p, take);
                c->used += take;
                p += take;
                n -= take;
                if (c->used == 64) {
                        sha1_block(c, c->block);
                        c->used = 0;
                }
        }
}

static void sha1_final(struct sha1 *c, uint8_t out[20])
{
        uint64_t bits = c->len * 8;
        uint8_t pad = 0x80, zero = 0, l[8];
        int i;
        sha1_update(c, &pad, 1);
        while (c->used != 56)
                sha1_update(c, &zero, 1);
        for (i = 0; i < 8; i++)
                l[i] = (uint8_t)(bits >> (56 - 8 * i));
        sha1_update(c, l, 8);
        for (i = 0; i < 5; i++) {
                out[4 * i] = (uint8_t)(c->h[i] >> 24);
                out[4 * i + 1] = (uint8_t)(c->h[i] >> 16);
                out[4 * i + 2] = (uint8_t)(c->h[i] >> 8);
                out[4 * i + 3] = (uint8_t)c->h[i];
        }
}

static void hmac_sha1(const uint8_t *key, int keylen, const uint8_t *data, size_t len, uint8_t out[20])
{
        uint8_t k[64] = { 0 }, pad[64], inner[20];
        struct sha1 c;
        int i;
        if (keylen > 64) {
                sha1_init(&c);
                sha1_update(&c, key, keylen);
                sha1_final(&c, k);
        } else
                memcpy(k, key, keylen);
        for (i = 0; i < 64; i++)
                pad[i] = k[i] ^ 0x36;
        sha1_init(&c);
        sha1_update(&c, pad, 64);
        sha1_update(&c, data, len);
        sha1_final(&c, inner);
        for (i = 0; i < 64; i++)
                pad[i] = k[i] ^ 0x5c;
        sha1_init(&c);
        sha1_update(&c, pad, 64);
        sha1_update(&c, inner, 20);
        sha1_final(&c, out);
}

static void nfold(const uint8_t *in, int inlen, uint8_t *out, int outlen)
{
        /* RFC 3961 n-fold: replicate the input rotated by 13 bits per copy and
         * add the outlen-byte chunks with end-around carry. */
        int a = inlen, b = outlen, lcm, i, carry = 0;
        while (b) {
                int t = a % b;
                a = b;
                b = t;
        }
        lcm = inlen * outlen / a;
        memset(out, 0, outlen);
        for (i = lcm - 1; i >= 0; i--) {
                int msbit = ((inlen << 3) - 1) + (((inlen << 3) + 13) * (i / inlen)) + ((inlen - (i % inlen)) << 3);
                int bit;
                msbit %= inlen << 3;
                bit = ((in[((inlen - 1) - (msbit >> 3)) % inlen] << 8)
                       | in[((inlen) - (msbit >> 3)) % inlen]) >> ((msbit & 7) + 1);
                bit &= 0xff;
                carry += bit + out[i % outlen];
                out[i % outlen] = (uint8_t)carry;
                carry >>= 8;
        }
        if (carry) {
                for (i = outlen - 1; i >= 0; i--) {
                        carry += out[i];
                        out[i] = (uint8_t)carry;
                        carry >>= 8;
                }
        }
}

static void derive(const uint8_t *key, int keylen, const uint8_t *constant, int clen, uint8_t *out)
{
        struct aes a;
        uint8_t block[16];
        int done = 0;

        aes_init(&a, key, keylen);
        nfold(constant, clen, block, 16);
        while (done < keylen) {
                aes_encrypt(&a, block, block);
                memcpy(out + done, block, keylen - done < 16 ? keylen - done : 16);
                done += 16;
        }
}

static void usage_key(const uint8_t *key, int keylen, uint32_t usage, uint8_t kind, uint8_t *out)
{
        uint8_t c[5] = { (uint8_t)(usage >> 24), (uint8_t)(usage >> 16), (uint8_t)(usage >> 8), (uint8_t)usage, kind };
        derive(key, keylen, c, 5, out);
}

static void string_to_key(const char *password, const char *salt, uint32_t iterations, uint8_t *key, int keylen)
{
        uint8_t tk[32];
        int block;

        for (block = 1; block * 20 - 20 < keylen; block++) {
                size_t slen = strlen(salt);
                uint8_t *s = malloc(slen + 4), u[20], t[20];
                uint32_t i;
                int j;
                if (!s)
                        return;
                memcpy(s, salt, slen);
                s[slen] = 0;
                s[slen + 1] = 0;
                s[slen + 2] = 0;
                s[slen + 3] = (uint8_t)block;
                hmac_sha1((const uint8_t *)password, (int)strlen(password), s, slen + 4, u);
                free(s);
                memcpy(t, u, 20);
                for (i = 1; i < iterations; i++) {
                        hmac_sha1((const uint8_t *)password, (int)strlen(password), u, 20, u);
                        for (j = 0; j < 20; j++)
                                t[j] ^= u[j];
                }
                memcpy(tk + (block - 1) * 20, t, keylen - (block - 1) * 20 < 20 ? keylen - (block - 1) * 20 : 20);
        }
        derive(tk, keylen, (const uint8_t *)"kerberos", 8, key);
}

static int random_bytes(uint8_t *p, size_t n)
{
        int fd = open("/dev/urandom", O_RDONLY);
        size_t got = 0;
        if (fd < 0)
                return -1;
        while (got < n) {
                ssize_t r = read(fd, p + got, n - got);
                if (r <= 0) {
                        close(fd);
                        return -1;
                }
                got += r;
        }
        close(fd);
        return 0;
}

/* AES-CTS (RFC 3962): CBC with zero IV, last two blocks swapped, final block truncated. */
static void cts_encrypt(const struct aes *a, const uint8_t *in, size_t len, uint8_t *out)
{
        uint8_t prev[16] = { 0 }, block[16];
        size_t n = (len + 15) / 16, i, j, last = len - 16 * (n - 1);
        uint8_t *tmp;

        if (len == 16) {
                aes_encrypt(a, in, out);
                return;
        }
        tmp = calloc(n, 16);
        if (!tmp)
                return;
        memcpy(tmp, in, len);
        for (i = 0; i < n; i++) {
                for (j = 0; j < 16; j++)
                        block[j] = tmp[16 * i + j] ^ prev[j];
                aes_encrypt(a, block, prev);
                memcpy(tmp + 16 * i, prev, 16);
        }
        memcpy(out, tmp, 16 * (n - 2));
        memcpy(out + 16 * (n - 2), tmp + 16 * (n - 1), 16);
        memcpy(out + 16 * (n - 1), tmp + 16 * (n - 2), last);
        free(tmp);
}

static void cts_decrypt(const struct aes *a, const uint8_t *in, size_t len, uint8_t *out)
{
        size_t n = (len + 15) / 16, i, j, last = len - 16 * (n - 1);
        uint8_t prev[16] = { 0 }, d[16], cn1[16];

        if (len == 16) {
                aes_decrypt(a, in, out);
                return;
        }
        for (i = 0; i + 2 < n; i++) {
                aes_decrypt(a, in + 16 * i, d);
                for (j = 0; j < 16; j++)
                        out[16 * i + j] = d[j] ^ prev[j];
                memcpy(prev, in + 16 * i, 16);
        }
        aes_decrypt(a, in + 16 * (n - 2), d);
        memcpy(cn1, in + 16 * (n - 1), last);
        memcpy(cn1 + last, d + last, 16 - last);
        for (j = 0; j < last; j++)
                out[16 * (n - 1) + j] = d[j] ^ in[16 * (n - 1) + j];
        aes_decrypt(a, cn1, d);
        for (j = 0; j < 16; j++)
                out[16 * (n - 2) + j] = d[j] ^ prev[j];
}

/* Returns a malloc'd confounder|plain ciphertext with a 12-byte HMAC-SHA1-96. */
static uint8_t *kenc(const uint8_t *key, int keylen, uint32_t usage, const uint8_t *plain, size_t len, size_t *outlen)
{
        uint8_t ke[32], ki[32], mac[20];
        uint8_t *data = malloc(len + 16), *out = malloc(len + 16 + 12);
        struct aes a;

        if (!data || !out || random_bytes(data, 16) < 0) {
                free(data);
                free(out);
                return NULL;
        }
        memcpy(data + 16, plain, len);
        usage_key(key, keylen, usage, 0xaa, ke);
        usage_key(key, keylen, usage, 0x55, ki);
        aes_init(&a, ke, keylen);
        cts_encrypt(&a, data, len + 16, out);
        hmac_sha1(ki, keylen, data, len + 16, mac);
        memcpy(out + len + 16, mac, 12);
        free(data);
        *outlen = len + 16 + 12;
        return out;
}

/* Returns a malloc'd plaintext without the confounder, or NULL on integrity failure. */
static uint8_t *kdec(const uint8_t *key, int keylen, uint32_t usage, const uint8_t *in, size_t len, size_t *outlen)
{
        uint8_t ke[32], ki[32], mac[20];
        uint8_t *data;
        struct aes a;

        if (len < 16 + 12)
                return NULL;
        data = malloc(len - 12);
        if (!data)
                return NULL;
        usage_key(key, keylen, usage, 0xaa, ke);
        usage_key(key, keylen, usage, 0x55, ki);
        aes_init(&a, ke, keylen);
        cts_decrypt(&a, in, len - 12, data);
        hmac_sha1(ki, keylen, data, len - 12, mac);
        if (memcmp(mac, in + len - 12, 12)) {
                free(data);
                return NULL;
        }
        memmove(data, data + 16, len - 12 - 16);
        *outlen = len - 12 - 16;
        return data;
}

static void kchecksum(const uint8_t *key, int keylen, uint32_t usage, const uint8_t *data, size_t len, uint8_t out[12])
{
        uint8_t kc[32], mac[20];
        usage_key(key, keylen, usage, 0x99, kc);
        hmac_sha1(kc, keylen, data, len, mac);
        memcpy(out, mac, 12);
}

/* ---------- DER ---------- */

struct buf {
        uint8_t *p;
        size_t n;
        int bad;
};

static struct buf raw(const void *p, size_t n)
{
        struct buf b = { malloc(n ? n : 1), n, 0 };
        if (!b.p)
                b.bad = 1;
        else if (n)
                memcpy(b.p, p, n);
        return b;
}

/* Consumes body. */
static struct buf tlv(uint8_t tag, struct buf body)
{
        struct buf b = { NULL, 0, body.bad };
        uint8_t head[6];
        size_t h = 0;

        if (body.bad) {
                free(body.p);
                return b;
        }
        head[h++] = tag;
        if (body.n < 128)
                head[h++] = (uint8_t)body.n;
        else if (body.n < 256) {
                head[h++] = 0x81;
                head[h++] = (uint8_t)body.n;
        } else if (body.n < 65536) {
                head[h++] = 0x82;
                head[h++] = (uint8_t)(body.n >> 8);
                head[h++] = (uint8_t)body.n;
        } else {
                head[h++] = 0x83;
                head[h++] = (uint8_t)(body.n >> 16);
                head[h++] = (uint8_t)(body.n >> 8);
                head[h++] = (uint8_t)body.n;
        }
        b.p = malloc(h + body.n);
        if (!b.p) {
                b.bad = 1;
                free(body.p);
                return b;
        }
        memcpy(b.p, head, h);
        memcpy(b.p + h, body.p, body.n);
        b.n = h + body.n;
        free(body.p);
        return b;
}

/* Concatenates and consumes count buffers. */
static struct buf cat(int count, ...)
{
        struct buf r = { NULL, 0, 0 }, parts[12];
        va_list ap;
        int i;
        size_t at = 0;

        va_start(ap, count);
        for (i = 0; i < count; i++) {
                parts[i] = va_arg(ap, struct buf);
                r.n += parts[i].n;
                r.bad |= parts[i].bad;
        }
        va_end(ap);
        r.p = malloc(r.n ? r.n : 1);
        if (!r.p)
                r.bad = 1;
        for (i = 0; i < count; i++) {
                if (r.p && parts[i].p)
                        memcpy(r.p + at, parts[i].p, parts[i].n);
                at += parts[i].n;
                free(parts[i].p);
        }
        return r;
}

#define SEQ(n, ...) tlv(0x30, cat(n, __VA_ARGS__))
#define CTX(i, x) tlv((uint8_t)(0xa0 + (i)), (x))
#define APP(i, x) tlv((uint8_t)(0x60 + (i)), (x))

static struct buf der_int(int64_t v)
{
        uint8_t b[9];
        int n = 0, i;
        for (i = 7; i >= 0; i--) {
                uint8_t byte = (uint8_t)(v >> (8 * i));
                if (n == 0 && i > 0) {
                        uint8_t next = (uint8_t)(v >> (8 * (i - 1)));
                        if ((byte == 0 && !(next & 0x80)) || (byte == 0xff && (next & 0x80)))
                                continue;
                }
                b[n++] = byte;
        }
        return tlv(0x02, raw(b, n));
}

static struct buf der_str(const char *s)
{
        return tlv(0x1b, raw(s, strlen(s)));
}

static struct buf der_oct(const uint8_t *p, size_t n)
{
        return tlv(0x04, raw(p, n));
}

static struct buf der_flags(uint32_t f)
{
        uint8_t b[5] = { 0, (uint8_t)(f >> 24), (uint8_t)(f >> 16), (uint8_t)(f >> 8), (uint8_t)f };
        return tlv(0x03, raw(b, 5));
}

static struct buf der_time(time_t t)
{
        struct tm tm;
        char s[20];
        gmtime_r(&t, &tm);
        strftime(s, sizeof(s), "%Y%m%d%H%M%SZ", &tm);
        return tlv(0x18, raw(s, strlen(s)));
}

static struct buf principal(int type, const char *a, const char *b)
{
        if (b)
                return SEQ(2, CTX(0, der_int(type)), CTX(1, SEQ(2, der_str(a), der_str(b))));
        return SEQ(2, CTX(0, der_int(type)), CTX(1, SEQ(1, der_str(a))));
}

struct der {
        const uint8_t *p;
        size_t n;
};

/* Reads one TLV from *in. Returns the tag, or -1. */
static int der_next(struct der *in, struct der *value)
{
        size_t len, h = 2;
        int tag;
        if (in->n < 2)
                return -1;
        tag = in->p[0];
        len = in->p[1];
        if (len & 0x80) {
                size_t k = len & 0x7f, i;
                if (k == 0 || k > 3 || in->n < 2 + k)
                        return -1;
                len = 0;
                for (i = 0; i < k; i++)
                        len = (len << 8) | in->p[2 + i];
                h += k;
        }
        if (in->n - h < len)
                return -1;
        value->p = in->p + h;
        value->n = len;
        in->p += h + len;
        in->n -= h + len;
        return tag;
}

/* Unwraps one TLV with the given tag (or any tag if tag < 0). */
static int der_open(struct der in, int tag, struct der *value)
{
        int t = der_next(&in, value);
        return t < 0 || (tag >= 0 && t != tag) ? -1 : 0;
}

/* Finds explicit field [n] in a SEQUENCE body; *value is the whole inner TLV. */
static int der_field(struct der seq, int n, struct der *value)
{
        struct der v;
        int t;
        while ((t = der_next(&seq, &v)) >= 0) {
                if (t == 0xa0 + n) {
                        *value = v;
                        return 0;
                }
        }
        return -1;
}

static int der_get_int(struct der field, int64_t *out)
{
        struct der v;
        size_t i;
        if (der_open(field, 0x02, &v) || v.n == 0 || v.n > 8)
                return -1;
        *out = (v.p[0] & 0x80) ? -1 : 0;
        for (i = 0; i < v.n; i++)
                *out = (*out << 8) | v.p[i];
        return 0;
}

/* Opens an APPLICATION/SEQUENCE wrapper chain: tag then SEQUENCE. */
static int der_message(struct der in, int app, struct der *seq)
{
        struct der v;
        if (der_open(in, app, &v))
                return -1;
        return der_open(v, 0x30, seq);
}

/* EncryptedData -> etype and cipher bytes. */
static int der_encrypted(struct der field, int64_t *etype, struct der *cipher)
{
        struct der seq, f;
        if (der_open(field, 0x30, &seq) || der_field(seq, 0, &f) || der_get_int(f, etype) || der_field(seq, 2, &f))
                return -1;
        return der_open(f, 0x04, cipher);
}

/* EncryptionKey -> key bytes. */
static int der_key(struct der field, uint8_t *key, int *keylen)
{
        struct der seq, f, v;
        int64_t type;
        if (der_open(field, 0x30, &seq) || der_field(seq, 0, &f) || der_get_int(f, &type) || der_field(seq, 1, &f)
            || der_open(f, 0x04, &v) || (v.n != 16 && v.n != 32))
                return -1;
        memcpy(key, v.p, v.n);
        *keylen = (int)v.n;
        return 0;
}

/* ---------- KDC transport ---------- */

static int kdc_exchange(struct smb2_context *smb2, const char *server, struct buf req, struct der *reply, uint8_t **storage)
{
        struct addrinfo hints, *ai = NULL;
        int fd = -1, e;
        uint8_t len[4];
        uint32_t n;
        size_t got;
        struct pollfd p;

        *storage = NULL;
        if (req.bad) {
                free(req.p);
                smb2_set_error(smb2, "Kerberos: out of memory");
                return -ENOMEM;
        }
        memset(&hints, 0, sizeof(hints));
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(server, "88", &hints, &ai) != 0 || !ai) {
                free(req.p);
                smb2_set_error(smb2, "Kerberos: cannot resolve %s", server);
                return -EHOSTUNREACH;
        }
        fd = socket(ai->ai_family, SOCK_STREAM, 0);
        if (fd < 0)
                goto unreachable;
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) != 0 && errno != EINPROGRESS)
                goto unreachable;
        p.fd = fd;
        p.events = POLLOUT;
        if (poll(&p, 1, KDC_TIMEOUT_MS) <= 0)
                goto unreachable;
        {
                socklen_t sl = sizeof(e);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &e, &sl) != 0 || e)
                        goto unreachable;
        }
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) & ~O_NONBLOCK);
        len[0] = (uint8_t)(req.n >> 24);
        len[1] = (uint8_t)(req.n >> 16);
        len[2] = (uint8_t)(req.n >> 8);
        len[3] = (uint8_t)req.n;
        if (send(fd, len, 4, 0) != 4 || send(fd, req.p, req.n, 0) != (ssize_t)req.n)
                goto unreachable;
        for (got = 0; got < 4;) {
                ssize_t r;
                p.events = POLLIN;
                if (poll(&p, 1, KDC_TIMEOUT_MS) <= 0 || (r = recv(fd, len + got, 4 - got, 0)) <= 0)
                        goto unreachable;
                got += r;
        }
        n = ((uint32_t)len[0] << 24) | ((uint32_t)len[1] << 16) | ((uint32_t)len[2] << 8) | len[3];
        if (n > 65536 || !(*storage = malloc(n ? n : 1)))
                goto unreachable;
        for (got = 0; got < n;) {
                ssize_t r;
                if (poll(&p, 1, KDC_TIMEOUT_MS) <= 0 || (r = recv(fd, *storage + got, n - got, 0)) <= 0)
                        goto unreachable;
                got += r;
        }
        close(fd);
        freeaddrinfo(ai);
        free(req.p);
        reply->p = *storage;
        reply->n = n;
        return 0;

unreachable:
        if (fd >= 0)
                close(fd);
        freeaddrinfo(ai);
        free(req.p);
        free(*storage);
        *storage = NULL;
        smb2_set_error(smb2, "Kerberos: no Mac login service (KDC) on %s", server);
        return -EHOSTUNREACH;
}

/* KRB-ERROR -> error code and, if present, crealm and e-data. */
static int krb_error(struct der msg, int64_t *code, char *crealm, size_t crealm_size, struct der *edata)
{
        struct der seq, f, v;
        if (der_message(msg, 0x7e, &seq) || der_field(seq, 6, &f) || der_get_int(f, code))
                return -1;
        if (crealm && !der_field(seq, 7, &f) && !der_open(f, 0x1b, &v) && v.n < crealm_size) {
                memcpy(crealm, v.p, v.n);
                crealm[v.n] = 0;
        }
        if (edata && (der_field(seq, 12, &f) || der_open(f, 0x04, edata)))
                edata->n = 0;
        return 0;
}

/* ---------- Kerberos exchanges ---------- */

struct private_auth_data {
        char realm[256];
        char user[256];
        uint8_t service_key[32];
        int service_keylen;
        struct buf ticket;      /* service Ticket TLV */
        uint8_t subkey[32];     /* initiator subkey */
        uint8_t acceptor[32];   /* acceptor subkey from AP-REP, if any */
        int acceptor_len;
        uint32_t seq;
        uint8_t *output;
        int output_len;
};

static struct buf as_req(const char *realm, const char *user, struct buf padata, int with_padata)
{
        uint32_t nonce;
        struct buf body;
        random_bytes((uint8_t *)&nonce, 4);
        /* forwardable, canonicalize (the LKDC referral needs it), renewable-ok; aes256 then aes128 */
        body = SEQ(7, CTX(0, der_flags(0x40010010)), CTX(1, principal(1, user, NULL)), CTX(2, der_str(realm)),
                   CTX(3, principal(2, "krbtgt", realm)), CTX(5, der_time(time(NULL) + 36000)),
                   CTX(7, der_int(nonce & 0x7fffffff)), CTX(8, SEQ(2, der_int(18), der_int(17))));
        if (with_padata)
                return APP(10, SEQ(4, CTX(1, der_int(5)), CTX(2, der_int(10)), CTX(3, SEQ(1, padata)), CTX(4, body)));
        free(padata.p);
        return APP(10, SEQ(3, CTX(1, der_int(5)), CTX(2, der_int(10)), CTX(4, body)));
}

/* Salt and iteration count from ETYPE-INFO2 in METHOD-DATA; defaults per RFC 3962. */
static void preauth_params(struct der edata, int64_t etype, char *salt, size_t salt_size, uint32_t *iterations)
{
        struct der list, pa, f, v, infos, info;
        int64_t type;
        if (der_open(edata, 0x30, &list))
                return;
        while (der_next(&list, &pa) == 0x30) {
                if (der_field(pa, 1, &f) || der_get_int(f, &type) || type != 19 || der_field(pa, 2, &f)
                    || der_open(f, 0x04, &v) || der_open(v, 0x30, &infos))
                        continue;
                while (der_next(&infos, &info) == 0x30) {
                        int64_t e;
                        struct der s;
                        if (der_field(info, 0, &f) || der_get_int(f, &e) || e != etype)
                                continue;
                        if (!der_field(info, 1, &f) && !der_open(f, 0x1b, &s) && s.n < salt_size) {
                                memcpy(salt, s.p, s.n);
                                salt[s.n] = 0;
                        }
                        if (!der_field(info, 2, &f) && !der_open(f, 0x04, &s) && s.n == 4)
                                *iterations = ((uint32_t)s.p[0] << 24) | ((uint32_t)s.p[1] << 16)
                                        | ((uint32_t)s.p[2] << 8) | s.p[3];
                }
        }
}

/* KDC-REP (AS 11 / TGS 13) -> Ticket TLV and session key from the encrypted part. */
static int kdc_rep(struct smb2_context *smb2, struct der msg, int app, const uint8_t *key, int keylen, uint32_t usage,
                   struct buf *ticket, uint8_t *session_key, int *session_keylen)
{
        struct der seq, f, cipher, inner, part, k;
        int64_t etype;
        uint8_t *plain;
        size_t plain_len;
        int ok;

        if (der_message(msg, app, &seq) || der_field(seq, 5, &f) || der_field(seq, 6, &part)
            || der_encrypted(part, &etype, &cipher)) {
                smb2_set_error(smb2, "Kerberos: malformed KDC reply");
                return -EINVAL;
        }
        *ticket = raw(f.p, f.n);
        plain = kdec(key, keylen, usage, cipher.p, cipher.n, &plain_len);
        if (!plain) {
                smb2_set_error(smb2, "Kerberos: wrong password");
                return -EACCES;
        }
        ok = der_open((struct der){ plain, plain_len }, -1, &inner) == 0 /* APPLICATION 25/26 */
             && der_open(inner, 0x30, &inner) == 0 && der_field(inner, 0, &k) == 0
             && der_key(k, session_key, session_keylen) == 0;
        free(plain);
        if (!ok) {
                smb2_set_error(smb2, "Kerberos: malformed KDC reply");
                return -EINVAL;
        }
        return 0;
}

static int login(struct smb2_context *smb2, struct private_auth_data *a, const char *server, const char *password)
{
        struct der reply, edata;
        uint8_t *storage, key[32], tgt_key[32];
        int keylen = 32, tgt_keylen, e;
        int64_t code, etype = 18;
        char salt[512];
        uint32_t iterations = 4096;
        struct buf tgt;

        /* 1. The Mac's realm: its KDC refers the well-known LKDC realm to the real one. */
        a->realm[0] = 0;
        if ((e = kdc_exchange(smb2, server, as_req(LKDC_WELLKNOWN, a->user, raw(NULL, 0), 0), &reply, &storage)))
                return e;
        e = krb_error(reply, &code, a->realm, sizeof(a->realm), NULL);
        free(storage);
        if (e || code != 68 || strncmp(a->realm, "LKDC:", 5)) {
                smb2_set_error(smb2, "Kerberos: %s has no Mac login service (LKDC)", server);
                return -EACCES;
        }

        /* 2. AS-REQ without pre-authentication returns the salt and iteration count. */
        snprintf(salt, sizeof(salt), "%s%s", a->realm, a->user);
        if ((e = kdc_exchange(smb2, server, as_req(a->realm, a->user, raw(NULL, 0), 0), &reply, &storage)))
                return e;
        if (krb_error(reply, &code, NULL, 0, &edata) == 0) {
                if (code == 6) {
                        free(storage);
                        smb2_set_error(smb2, "Kerberos: unknown user %s", a->user);
                        return -EACCES;
                }
                if (code == 25 && edata.n) {
                        struct der list = edata;
                        preauth_params(list, 18, salt, sizeof(salt), &iterations);
                }
        }
        free(storage);
        if (iterations == 0 || iterations > 1000000)
                iterations = 4096;
        string_to_key(password, salt, iterations, key, keylen);

        /* 3. AS-REQ with PA-ENC-TIMESTAMP. */
        {
                struct buf ts = SEQ(2, CTX(0, der_time(time(NULL))), CTX(1, der_int(0)));
                size_t clen;
                uint8_t *c = ts.bad ? NULL : kenc(key, keylen, 1, ts.p, ts.n, &clen);
                struct buf pa;
                free(ts.p);
                if (!c) {
                        smb2_set_error(smb2, "Kerberos: out of memory");
                        return -ENOMEM;
                }
                pa = SEQ(2, CTX(1, der_int(2)),
                         CTX(2, tlv(0x04, SEQ(2, CTX(0, der_int(etype)), CTX(2, der_oct(c, clen))))));
                free(c);
                if ((e = kdc_exchange(smb2, server, as_req(a->realm, a->user, pa, 1), &reply, &storage)))
                        return e;
        }
        if (krb_error(reply, &code, NULL, 0, NULL) == 0) {
                free(storage);
                /* Heimdal answers a bad timestamp with PREAUTH_REQUIRED (25), not 24. */
                smb2_set_error(smb2, code == 24 || code == 25 || code == 31 ? "Kerberos: wrong password"
                                                              : "Kerberos: login refused (error %d)", (int)code);
                return -EACCES;
        }
        e = kdc_rep(smb2, reply, 0x6b, key, keylen, 3, &tgt, tgt_key, &tgt_keylen);
        free(storage);
        if (e)
                return e;

        /* 4. TGS-REQ for cifs/<realm>@<realm>, the Mac's SMB service. */
        {
                uint32_t nonce;
                struct buf body, auth, apreq, req;
                uint8_t cksum[12], *c;
                size_t clen;
                time_t now = time(NULL);

                random_bytes((uint8_t *)&nonce, 4);
                body = SEQ(6, CTX(0, der_flags(0x00010000)), CTX(2, der_str(a->realm)),
                           CTX(3, principal(2, "cifs", a->realm)), CTX(5, der_time(now + 36000)),
                           CTX(7, der_int(nonce & 0x7fffffff)), CTX(8, SEQ(2, der_int(18), der_int(17))));
                if (body.bad) {
                        free(body.p);
                        free(tgt.p);
                        return -ENOMEM;
                }
                kchecksum(tgt_key, tgt_keylen, 6, body.p, body.n, cksum);
                auth = APP(2, SEQ(6, CTX(0, der_int(5)), CTX(1, der_str(a->realm)), CTX(2, principal(1, a->user, NULL)),
                                  CTX(3, SEQ(2, CTX(0, der_int(tgt_keylen == 32 ? 16 : 15)), CTX(1, der_oct(cksum, 12)))),
                                  CTX(4, der_int(0)), CTX(5, der_time(now))));
                c = auth.bad ? NULL : kenc(tgt_key, tgt_keylen, 7, auth.p, auth.n, &clen);
                free(auth.p);
                if (!c) {
                        free(body.p);
                        free(tgt.p);
                        return -ENOMEM;
                }
                apreq = APP(14, SEQ(5, CTX(0, der_int(5)), CTX(1, der_int(14)), CTX(2, der_flags(0)), CTX(3, tgt),
                                    CTX(4, SEQ(2, CTX(0, der_int(tgt_keylen == 32 ? 18 : 17)), CTX(2, der_oct(c, clen))))));
                free(c);
                req = APP(12, SEQ(4, CTX(1, der_int(5)), CTX(2, der_int(12)),
                                  CTX(3, SEQ(1, SEQ(2, CTX(1, der_int(1)), CTX(2, tlv(0x04, apreq))))), CTX(4, body)));
                if ((e = kdc_exchange(smb2, server, req, &reply, &storage)))
                        return e;
        }
        if (krb_error(reply, &code, NULL, 0, NULL) == 0) {
                free(storage);
                smb2_set_error(smb2, "Kerberos: no SMB service ticket (error %d)", (int)code);
                return -EACCES;
        }
        e = kdc_rep(smb2, reply, 0x6d, tgt_key, tgt_keylen, 8, &a->ticket, a->service_key, &a->service_keylen);
        free(storage);
        return e;
}

static const uint8_t oid_krb5[] = { 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x12, 0x01, 0x02, 0x02 };
static const uint8_t oid_spnego[] = { 0x06, 0x06, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x02 };

/* SPNEGO NegTokenInit carrying a GSS krb5 AP-REQ with mutual authentication. */
static struct buf negotiate_token(struct private_auth_data *a)
{
        uint8_t gss_cksum[24] = { 16 }, *c;
        struct buf auth, apreq;
        size_t clen;
        time_t now = time(NULL);
        int etype = a->service_keylen == 32 ? 18 : 17;

        /* RFC 4121 4.1.1: no channel bindings; mutual, replay, sequence, confidentiality, integrity. */
        gss_cksum[20] = 0x3e;
        auth = APP(2, SEQ(8, CTX(0, der_int(5)), CTX(1, der_str(a->realm)), CTX(2, principal(1, a->user, NULL)),
                          CTX(3, SEQ(2, CTX(0, der_int(0x8003)), CTX(1, der_oct(gss_cksum, 24)))),
                          CTX(4, der_int(0)), CTX(5, der_time(now)),
                          CTX(6, SEQ(2, CTX(0, der_int(etype)), CTX(1, der_oct(a->subkey, a->service_keylen)))),
                          CTX(7, der_int(a->seq))));
        c = auth.bad ? NULL : kenc(a->service_key, a->service_keylen, 11, auth.p, auth.n, &clen);
        free(auth.p);
        if (!c) {
                struct buf bad = { NULL, 0, 1 };
                return bad;
        }
        apreq = APP(14, SEQ(5, CTX(0, der_int(5)), CTX(1, der_int(14)), CTX(2, der_flags(0x20000000)),
                            CTX(3, raw(a->ticket.p, a->ticket.n)),
                            CTX(4, SEQ(2, CTX(0, der_int(etype)), CTX(2, der_oct(c, clen))))));
        free(c);
        return APP(0, cat(2, raw(oid_spnego, sizeof(oid_spnego)),
                          CTX(0, SEQ(2, CTX(0, SEQ(1, raw(oid_krb5, sizeof(oid_krb5)))),
                                     CTX(2, tlv(0x04, APP(0, cat(3, raw(oid_krb5, sizeof(oid_krb5)),
                                                                    raw("\x01\x00", 2), apreq))))))));
}

/* SPNEGO NegTokenResp: rejects, or takes the acceptor subkey from the AP-REP. */
static int accept_token(struct smb2_context *smb2, struct private_auth_data *a, const uint8_t *buf, int len)
{
        struct der in = { buf, (size_t)len }, seq, f, v, token, rep;
        int64_t state, etype;
        uint8_t *plain;
        size_t plain_len;

        if (der_open(in, 0xa1, &v) || der_open(v, 0x30, &seq))
                return 0; /* no NegTokenResp: nothing to check */
        if (!der_field(seq, 0, &f) && !der_open(f, 0x0a, &v) && v.n == 1) {
                state = v.p[0];
                if (state == 2) {
                        smb2_set_error(smb2, "Kerberos: the Mac rejected the login");
                        return -EACCES;
                }
        }
        if (der_field(seq, 2, &f) || der_open(f, 0x04, &token) || der_open(token, 0x60, &v))
                return 0;
        /* GSS header: krb5 OID, token id 02 00 (AP-REP) or 03 00 (KRB-ERROR). */
        if (v.n < sizeof(oid_krb5) + 2 || memcmp(v.p, oid_krb5, sizeof(oid_krb5)))
                return 0;
        if (v.p[sizeof(oid_krb5)] != 0x02) {
                smb2_set_error(smb2, "Kerberos: the Mac rejected the login");
                return -EACCES;
        }
        rep.p = v.p + sizeof(oid_krb5) + 2;
        rep.n = v.n - sizeof(oid_krb5) - 2;
        if (der_message(rep, 0x6f, &seq) || der_field(seq, 2, &f) || der_encrypted(f, &etype, &v))
                return 0;
        plain = kdec(a->service_key, a->service_keylen, 12, v.p, v.n, &plain_len);
        if (!plain) {
                smb2_set_error(smb2, "Kerberos: the Mac's reply failed verification");
                return -EACCES;
        }
        if (!der_message((struct der){ plain, plain_len }, 0x7b, &seq) && !der_field(seq, 2, &f))
                der_key(f, a->acceptor, &a->acceptor_len);
        free(plain);
        return 0;
}

/* ---------- libsmb2 entry points ---------- */

struct private_auth_data *krb5_negotiate_reply(struct smb2_context *smb2, const char *server,
                                               const char *domain, const char *user_name,
                                               const char *password)
{
        struct private_auth_data *a;
        (void)domain;

        if (!user_name || !*user_name || !password || !*password) {
                smb2_set_error(smb2, "Kerberos: a user name and password are required");
                return NULL;
        }
        a = calloc(1, sizeof(*a));
        if (!a)
                return NULL;
        snprintf(a->user, sizeof(a->user), "%s", user_name);
        if (login(smb2, a, server, password) < 0 || random_bytes(a->subkey, sizeof(a->subkey)) < 0
            || random_bytes((uint8_t *)&a->seq, 4) < 0) {
                krb5_free_auth_data(a);
                return NULL;
        }
        a->seq &= 0x3fffffff;
        return a;
}

int krb5_session_request(struct smb2_context *smb2, struct private_auth_data *a, unsigned char *buf, int len)
{
        free(a->output);
        a->output = NULL;
        a->output_len = 0;
        if (buf && len > 0)
                return accept_token(smb2, a, buf, len);
        {
                struct buf t = negotiate_token(a);
                if (t.bad) {
                        free(t.p);
                        smb2_set_error(smb2, "Kerberos: out of memory");
                        return -ENOMEM;
                }
                a->output = t.p;
                a->output_len = (int)t.n;
        }
        return 0;
}

int krb5_get_output_token_length(struct private_auth_data *a)
{
        return a->output_len;
}

unsigned char *krb5_get_output_token_buffer(struct private_auth_data *a)
{
        return a->output;
}

int krb5_session_get_session_key(struct smb2_context *smb2, struct private_auth_data *a)
{
        /* MS-SMB2 3.2.5.3.1: the first 16 bytes of the GSS key; the acceptor subkey wins. */
        const uint8_t *key = a->acceptor_len ? a->acceptor : a->subkey;
        free(smb2->session_key);
        smb2->session_key = malloc(16);
        if (!smb2->session_key)
                return -1;
        memcpy(smb2->session_key, key, 16);
        smb2->session_key_size = 16;
        return 0;
}

void krb5_free_auth_data(struct private_auth_data *a)
{
        if (!a)
                return;
        free(a->ticket.p);
        free(a->output);
        memset(a, 0, sizeof(*a));
        free(a);
}
