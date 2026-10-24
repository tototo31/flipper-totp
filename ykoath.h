#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <lib/nfc/protocols/iso14443_4a/iso14443_4a_poller.h>

#ifdef __cplusplus
extern "C" {
#endif

#define YKOATH_AID_LEN 7
#define YKOATH_NAME_MAX 64
#define YKOATH_CODE_MAX 16
#define YKOATH_CRED_MAX 32
#define YKOATH_CHALLENGE_MAX 16
#define YKOATH_ID_MAX 16
#define YKOATH_RESP_MAX 768
#define YKOATH_KEY_MAX 64
#define YKOATH_SECRET_TEXT_MAX 128

#define YKOATH_TYPE_HOTP 0x10
#define YKOATH_TYPE_TOTP 0x20
#define YKOATH_ALG_SHA1 0x01
#define YKOATH_ALG_SHA256 0x02
#define YKOATH_ALG_SHA512 0x03

static const uint8_t YkoathAid[YKOATH_AID_LEN] = {0xA0, 0x00, 0x00, 0x05, 0x27, 0x21, 0x01};

typedef enum {
    YkoathOk = 0,
    YkoathErrorTransport,
    YkoathErrorSw,
    YkoathErrorParse,
    YkoathErrorAuth,
    YkoathErrorNoSpace,
    YkoathErrorNeedPin,
    YkoathErrorNotFound,
    YkoathErrorSyntax,
    YkoathErrorNoApplet,
} YkoathResult;

typedef struct {
    uint8_t version[3];
    uint8_t id[YKOATH_ID_MAX];
    size_t id_len;
    uint8_t challenge[YKOATH_CHALLENGE_MAX];
    size_t challenge_len;
    uint8_t algorithm; /* 0 if absent */
    bool has_challenge;
} YkoathSelectInfo;

typedef struct {
    char name[YKOATH_NAME_MAX];
    uint8_t algo; /* high nibble type, low nibble HMAC alg (LIST); 0 if unknown */
    uint8_t digits;
    char code[YKOATH_CODE_MAX]; /* empty for LIST-only entries */
} YkoathCredential;

typedef struct {
    YkoathCredential items[YKOATH_CRED_MAX];
    size_t count;
} YkoathCredentialList;

typedef struct {
    char name[YKOATH_NAME_MAX];
    uint8_t key[YKOATH_KEY_MAX];
    size_t key_len;
    uint8_t digits;
    uint8_t type_alg; /* e.g. 0x21 = TOTP|SHA1 */
} YkoathPutParams;

/** Send one ISO7816 APDU over ISO14443-4A; appends SEND REMAINING while SW=61xx. */
YkoathResult ykoath_transceive(
    Iso14443_4aPoller* poller,
    const uint8_t* apdu,
    size_t apdu_len,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len,
    uint16_t* sw);

YkoathResult ykoath_select(Iso14443_4aPoller* poller, YkoathSelectInfo* info);

/**
 * VALIDATE using PBKDF2-HMAC-SHA1 (1000 iters, salt=SELECT id, 16-byte key)
 * and mutual HMAC-SHA1, matching VivoKey apex-totp client.pl.
 */
YkoathResult ykoath_validate(
    Iso14443_4aPoller* poller,
    const YkoathSelectInfo* select,
    const char* password);

/** SELECT + optional VALIDATE. Returns NeedPin if challenge and no password. */
YkoathResult ykoath_open(
    Iso14443_4aPoller* poller,
    const char* password_or_null,
    YkoathSelectInfo* select_out);

YkoathResult ykoath_list(Iso14443_4aPoller* poller, YkoathCredentialList* list);

YkoathResult ykoath_calculate(
    Iso14443_4aPoller* poller,
    const char* name,
    uint64_t unix_time,
    YkoathCredential* out);

YkoathResult ykoath_calculate_all(
    Iso14443_4aPoller* poller,
    uint64_t unix_time,
    YkoathCredentialList* list);

YkoathResult ykoath_put(Iso14443_4aPoller* poller, const YkoathPutParams* params);

YkoathResult ykoath_delete(Iso14443_4aPoller* poller, const char* name);

/** Full CALCULATE ALL path inside a poller Ready callback. */
YkoathResult ykoath_read_totp(
    Iso14443_4aPoller* poller,
    const char* password_or_null,
    uint64_t unix_time,
    YkoathSelectInfo* select_out,
    YkoathCredentialList* list);

/**
 * Decode a TOTP secret from Base32 or hex (optional spaces).
 * Returns false on invalid input.
 */
bool ykoath_parse_secret(const char* text, uint8_t* out, size_t out_cap, size_t* out_len);

const char* ykoath_result_str(YkoathResult result);

#ifdef __cplusplus
}
#endif
