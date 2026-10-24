#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRYPTO_SHA1_SIZE 20
#define CRYPTO_OATH_KEY_SIZE 16
#define CRYPTO_OATH_PBKDF2_ITERS 1000

/** HMAC-SHA1(key, msg) → 20-byte digest. */
bool crypto_hmac_sha1(
    const uint8_t* key,
    size_t key_len,
    const uint8_t* msg,
    size_t msg_len,
    uint8_t out[CRYPTO_SHA1_SIZE]);

/**
 * PBKDF2-HMAC-SHA1 as used by YKOATH SET CODE / VALIDATE
 * (1000 iterations, 16-byte derived key; salt = SELECT id).
 */
bool crypto_pbkdf2_hmac_sha1(
    const uint8_t* password,
    size_t password_len,
    const uint8_t* salt,
    size_t salt_len,
    uint32_t iterations,
    uint8_t* out,
    size_t out_len);

#ifdef __cplusplus
}
#endif
