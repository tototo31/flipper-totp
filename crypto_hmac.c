#include "crypto_hmac.h"

#include <string.h>

/* Compact SHA-1 + HMAC + PBKDF2 for FAP use (mbedtls HMAC is not exported). */

typedef struct {
    uint32_t state[5];
    uint64_t bitcount;
    uint8_t buffer[64];
    size_t buffer_len;
} Sha1Ctx;

static uint32_t sha1_rol(uint32_t value, uint32_t bits) {
    return (value << bits) | (value >> (32 - bits));
}

static void sha1_process_block(Sha1Ctx* ctx, const uint8_t block[64]) {
    uint32_t w[80];
    for(size_t i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) | (uint32_t)block[i * 4 + 3];
    }
    for(size_t i = 16; i < 80; i++) {
        w[i] = sha1_rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint32_t e = ctx->state[4];

    for(size_t i = 0; i < 80; i++) {
        uint32_t f;
        uint32_t k;
        if(i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999u;
        } else if(i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
        } else if(i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
        }
        uint32_t temp = sha1_rol(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = sha1_rol(b, 30);
        b = a;
        a = temp;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
}

static void sha1_init(Sha1Ctx* ctx) {
    ctx->state[0] = 0x67452301u;
    ctx->state[1] = 0xEFCDAB89u;
    ctx->state[2] = 0x98BADCFEu;
    ctx->state[3] = 0x10325476u;
    ctx->state[4] = 0xC3D2E1F0u;
    ctx->bitcount = 0;
    ctx->buffer_len = 0;
}

static void sha1_update(Sha1Ctx* ctx, const uint8_t* data, size_t len) {
    ctx->bitcount += (uint64_t)len * 8;
    while(len > 0) {
        size_t take = 64 - ctx->buffer_len;
        if(take > len) take = len;
        memcpy(ctx->buffer + ctx->buffer_len, data, take);
        ctx->buffer_len += take;
        data += take;
        len -= take;
        if(ctx->buffer_len == 64) {
            sha1_process_block(ctx, ctx->buffer);
            ctx->buffer_len = 0;
        }
    }
}

static void sha1_final(Sha1Ctx* ctx, uint8_t out[CRYPTO_SHA1_SIZE]) {
    uint8_t pad[64 + 8];
    size_t pad_len = (ctx->buffer_len < 56) ? (56 - ctx->buffer_len) : (120 - ctx->buffer_len);
    memset(pad, 0, pad_len);
    pad[0] = 0x80;

    uint64_t bits = ctx->bitcount;
    uint8_t len_be[8];
    for(int i = 7; i >= 0; i--) {
        len_be[i] = (uint8_t)(bits & 0xff);
        bits >>= 8;
    }

    sha1_update(ctx, pad, pad_len);
    sha1_update(ctx, len_be, 8);

    for(size_t i = 0; i < 5; i++) {
        out[i * 4] = (uint8_t)(ctx->state[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        out[i * 4 + 3] = (uint8_t)(ctx->state[i]);
    }
}

bool crypto_hmac_sha1(
    const uint8_t* key,
    size_t key_len,
    const uint8_t* msg,
    size_t msg_len,
    uint8_t out[CRYPTO_SHA1_SIZE]) {
    if(!key || !msg || !out) return false;

    uint8_t key_block[64];
    memset(key_block, 0, sizeof(key_block));
    if(key_len > 64) {
        Sha1Ctx ctx;
        sha1_init(&ctx);
        sha1_update(&ctx, key, key_len);
        sha1_final(&ctx, key_block);
    } else {
        memcpy(key_block, key, key_len);
    }

    uint8_t ipad[64];
    uint8_t opad[64];
    for(size_t i = 0; i < 64; i++) {
        ipad[i] = key_block[i] ^ 0x36;
        opad[i] = key_block[i] ^ 0x5c;
    }

    Sha1Ctx ctx;
    uint8_t inner[CRYPTO_SHA1_SIZE];
    sha1_init(&ctx);
    sha1_update(&ctx, ipad, 64);
    sha1_update(&ctx, msg, msg_len);
    sha1_final(&ctx, inner);

    sha1_init(&ctx);
    sha1_update(&ctx, opad, 64);
    sha1_update(&ctx, inner, CRYPTO_SHA1_SIZE);
    sha1_final(&ctx, out);

    memset(key_block, 0, sizeof(key_block));
    memset(ipad, 0, sizeof(ipad));
    memset(opad, 0, sizeof(opad));
    memset(inner, 0, sizeof(inner));
    return true;
}

bool crypto_pbkdf2_hmac_sha1(
    const uint8_t* password,
    size_t password_len,
    const uint8_t* salt,
    size_t salt_len,
    uint32_t iterations,
    uint8_t* out,
    size_t out_len) {
    if(!password || !salt || !out || iterations == 0 || out_len == 0) return false;

    uint32_t block_index = 1;
    size_t offset = 0;
    while(offset < out_len) {
        uint8_t block[CRYPTO_SHA1_SIZE];
        uint8_t u[CRYPTO_SHA1_SIZE];
        uint8_t salt_block[128];
        if(salt_len + 4 > sizeof(salt_block)) return false;

        memcpy(salt_block, salt, salt_len);
        salt_block[salt_len] = (uint8_t)(block_index >> 24);
        salt_block[salt_len + 1] = (uint8_t)(block_index >> 16);
        salt_block[salt_len + 2] = (uint8_t)(block_index >> 8);
        salt_block[salt_len + 3] = (uint8_t)(block_index);

        if(!crypto_hmac_sha1(password, password_len, salt_block, salt_len + 4, u)) {
            return false;
        }
        memcpy(block, u, CRYPTO_SHA1_SIZE);

        for(uint32_t i = 1; i < iterations; i++) {
            if(!crypto_hmac_sha1(password, password_len, u, CRYPTO_SHA1_SIZE, u)) {
                return false;
            }
            for(size_t j = 0; j < CRYPTO_SHA1_SIZE; j++) {
                block[j] ^= u[j];
            }
        }

        size_t copy = out_len - offset;
        if(copy > CRYPTO_SHA1_SIZE) copy = CRYPTO_SHA1_SIZE;
        memcpy(out + offset, block, copy);
        offset += copy;
        block_index++;
    }

    return true;
}
