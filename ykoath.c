#include "ykoath.h"
#include "crypto_hmac.h"

#include <furi.h>
#include <furi_hal_random.h>
#include <toolbox/bit_buffer.h>

#include <string.h>
#include <stdio.h>

#define TAG "Ykoath"

#define YKOATH_TAG_NAME 0x71
#define YKOATH_TAG_NAME_LIST 0x72
#define YKOATH_TAG_KEY 0x73
#define YKOATH_TAG_CHALLENGE 0x74
#define YKOATH_TAG_RESPONSE 0x75
#define YKOATH_TAG_T_RESPONSE 0x76
#define YKOATH_TAG_NO_RESPONSE 0x77
#define YKOATH_TAG_PROPERTY_TOUCH 0x7c
#define YKOATH_TAG_VERSION 0x79
#define YKOATH_TAG_ALGORITHM 0x7b

#define YKOATH_TX_CAP 512
#define YKOATH_RX_CAP 512
#define YKOATH_TX_RETRIES 4
#define YKOATH_TX_RETRY_DELAY_MS 15

static Iso14443_4aError ykoath_send_block(
    Iso14443_4aPoller* poller,
    const BitBuffer* tx,
    BitBuffer* rx) {
    /* Prefer PWT-ext path: handles S(WTX) and longer card compute better. */
    Iso14443_4aError err = iso14443_4a_poller_send_block_pwt_ext(poller, tx, rx);
    if(err == Iso14443_4aErrorNone) {
        return err;
    }
    bit_buffer_reset(rx);
    return iso14443_4a_poller_send_block(poller, tx, rx);
}

static size_t ykoath_tlv_len_size(uint8_t first) {
    if(first < 0x80) return 1;
    if(first == 0x81) return 2;
    if(first == 0x82) return 3;
    return 0;
}

static bool ykoath_read_tlv_len(const uint8_t* buf, size_t buf_len, size_t* offs, size_t* len_out) {
    if(*offs >= buf_len) return false;
    uint8_t first = buf[*offs];
    size_t n = ykoath_tlv_len_size(first);
    if(n == 0 || *offs + n > buf_len) return false;
    size_t len;
    if(n == 1) {
        len = first;
    } else if(n == 2) {
        len = buf[*offs + 1];
    } else {
        len = ((size_t)buf[*offs + 1] << 8) | buf[*offs + 2];
    }
    *offs += n;
    *len_out = len;
    return true;
}

static bool ykoath_append_tlv(uint8_t* buf, size_t cap, size_t* len, uint8_t tag, const uint8_t* data, size_t data_len) {
    if(data_len > 0x7f) return false; /* short-form only for requests we build */
    if(*len + 2 + data_len > cap) return false;
    buf[(*len)++] = tag;
    buf[(*len)++] = (uint8_t)data_len;
    if(data_len) {
        memcpy(buf + *len, data, data_len);
        *len += data_len;
    }
    return true;
}

static uint32_t ykoath_pow10(uint8_t digits) {
    uint32_t v = 1;
    while(digits--) v *= 10;
    return v;
}

static void ykoath_format_code(uint8_t digits, const uint8_t code4[4], char* out, size_t out_len) {
    uint32_t raw = ((uint32_t)code4[0] << 24) | ((uint32_t)code4[1] << 16) |
                   ((uint32_t)code4[2] << 8) | (uint32_t)code4[3];
    if(digits < 1) digits = 6;
    if(digits > 9) digits = 9;
    uint32_t code = raw % ykoath_pow10(digits);
    snprintf(out, out_len, "%0*lu", (int)digits, (unsigned long)code);
}

YkoathResult ykoath_transceive(
    Iso14443_4aPoller* poller,
    const uint8_t* apdu,
    size_t apdu_len,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len,
    uint16_t* sw) {
    furi_check(poller && apdu && out && out_len && sw);

    BitBuffer* tx = bit_buffer_alloc(YKOATH_TX_CAP);
    BitBuffer* rx = bit_buffer_alloc(YKOATH_RX_CAP);
    *out_len = 0;
    *sw = 0;

    Iso14443_4aError err = Iso14443_4aErrorTimeout;
    for(uint8_t attempt = 0; attempt < YKOATH_TX_RETRIES; attempt++) {
        bit_buffer_reset(tx);
        bit_buffer_reset(rx);
        bit_buffer_copy_bytes(tx, apdu, apdu_len);
        err = ykoath_send_block(poller, tx, rx);
        if(err == Iso14443_4aErrorNone && bit_buffer_get_size_bytes(rx) >= 2) {
            break;
        }
        FURI_LOG_W(
            TAG,
            "APDU INS=%02X attempt %u err=%d rx=%u",
            apdu_len > 1 ? apdu[1] : 0,
            attempt + 1,
            err,
            (unsigned)bit_buffer_get_size_bytes(rx));
        furi_delay_ms(YKOATH_TX_RETRY_DELAY_MS);
    }

    if(err != Iso14443_4aErrorNone || bit_buffer_get_size_bytes(rx) < 2) {
        FURI_LOG_E(TAG, "send_block failed: %d", err);
        bit_buffer_free(tx);
        bit_buffer_free(rx);
        return YkoathErrorTransport;
    }

    size_t rx_len = bit_buffer_get_size_bytes(rx);
    const uint8_t* rx_data = bit_buffer_get_data(rx);

    size_t data_len = rx_len - 2;
    if(data_len > out_cap) {
        bit_buffer_free(tx);
        bit_buffer_free(rx);
        return YkoathErrorNoSpace;
    }
    memcpy(out, rx_data, data_len);
    *out_len = data_len;
    *sw = ((uint16_t)rx_data[rx_len - 2] << 8) | rx_data[rx_len - 1];

    while((*sw & 0xFF00) == 0x6100) {
        uint8_t rem_apdu[5] = {0x00, 0xA5, 0x00, 0x00, 0x00};
        err = Iso14443_4aErrorTimeout;
        for(uint8_t attempt = 0; attempt < YKOATH_TX_RETRIES; attempt++) {
            bit_buffer_reset(tx);
            bit_buffer_reset(rx);
            bit_buffer_copy_bytes(tx, rem_apdu, sizeof(rem_apdu));
            err = ykoath_send_block(poller, tx, rx);
            if(err == Iso14443_4aErrorNone && bit_buffer_get_size_bytes(rx) >= 2) {
                break;
            }
            furi_delay_ms(YKOATH_TX_RETRY_DELAY_MS);
        }
        if(err != Iso14443_4aErrorNone || bit_buffer_get_size_bytes(rx) < 2) {
            bit_buffer_free(tx);
            bit_buffer_free(rx);
            return YkoathErrorTransport;
        }
        rx_len = bit_buffer_get_size_bytes(rx);
        rx_data = bit_buffer_get_data(rx);
        data_len = rx_len - 2;
        if(*out_len + data_len > out_cap) {
            bit_buffer_free(tx);
            bit_buffer_free(rx);
            return YkoathErrorNoSpace;
        }
        memcpy(out + *out_len, rx_data, data_len);
        *out_len += data_len;
        *sw = ((uint16_t)rx_data[rx_len - 2] << 8) | rx_data[rx_len - 1];
    }

    bit_buffer_free(tx);
    bit_buffer_free(rx);
    return YkoathOk;
}

static YkoathResult ykoath_parse_select(const uint8_t* data, size_t len, YkoathSelectInfo* info) {
    memset(info, 0, sizeof(*info));
    size_t offs = 0;
    while(offs + 1 < len) {
        uint8_t tag = data[offs++];
        size_t tlen = 0;
        if(!ykoath_read_tlv_len(data, len, &offs, &tlen)) return YkoathErrorParse;
        if(offs + tlen > len) return YkoathErrorParse;
        const uint8_t* val = data + offs;
        if(tag == YKOATH_TAG_VERSION && tlen >= 3) {
            memcpy(info->version, val, 3);
        } else if(tag == YKOATH_TAG_NAME) {
            size_t copy = tlen;
            if(copy > YKOATH_ID_MAX) copy = YKOATH_ID_MAX;
            memcpy(info->id, val, copy);
            info->id_len = copy;
        } else if(tag == YKOATH_TAG_CHALLENGE) {
            size_t copy = tlen;
            if(copy > YKOATH_CHALLENGE_MAX) copy = YKOATH_CHALLENGE_MAX;
            memcpy(info->challenge, val, copy);
            info->challenge_len = copy;
            info->has_challenge = true;
        } else if(tag == YKOATH_TAG_ALGORITHM && tlen >= 1) {
            info->algorithm = val[0];
        }
        offs += tlen;
    }
    return YkoathOk;
}

YkoathResult ykoath_select(Iso14443_4aPoller* poller, YkoathSelectInfo* info) {
    uint8_t apdu[5 + YKOATH_AID_LEN] = {
        0x00, 0xA4, 0x04, 0x00, YKOATH_AID_LEN};
    memcpy(apdu + 5, YkoathAid, YKOATH_AID_LEN);

    uint8_t resp[YKOATH_RESP_MAX];
    size_t resp_len = 0;
    uint16_t sw = 0;
    YkoathResult r = ykoath_transceive(poller, apdu, sizeof(apdu), resp, sizeof(resp), &resp_len, &sw);
    if(r != YkoathOk) return r;
    if(sw != 0x9000) {
        FURI_LOG_E(TAG, "SELECT SW=%04X", sw);
        if(sw == 0x6A82 || sw == 0x6A88 || sw == 0x6A86) {
            return YkoathErrorNoApplet;
        }
        return YkoathErrorSw;
    }
    return ykoath_parse_select(resp, resp_len, info);
}

YkoathResult ykoath_validate(
    Iso14443_4aPoller* poller,
    const YkoathSelectInfo* select,
    const char* password) {
    furi_check(poller && select && password);
    if(!select->has_challenge || select->challenge_len == 0) return YkoathOk;

    uint8_t key[CRYPTO_OATH_KEY_SIZE];
    if(!crypto_pbkdf2_hmac_sha1(
           (const uint8_t*)password,
           strlen(password),
           select->id,
           select->id_len,
           CRYPTO_OATH_PBKDF2_ITERS,
           key,
           sizeof(key))) {
        return YkoathErrorAuth;
    }

    uint8_t host_resp[CRYPTO_SHA1_SIZE];
    if(!crypto_hmac_sha1(key, sizeof(key), select->challenge, select->challenge_len, host_resp)) {
        memset(key, 0, sizeof(key));
        return YkoathErrorAuth;
    }

    uint8_t host_chal[8];
    furi_hal_random_fill_buf(host_chal, sizeof(host_chal));

    uint8_t body[128];
    size_t body_len = 0;
    if(!ykoath_append_tlv(body, sizeof(body), &body_len, YKOATH_TAG_RESPONSE, host_resp, CRYPTO_SHA1_SIZE)) {
        memset(key, 0, sizeof(key));
        return YkoathErrorNoSpace;
    }
    if(!ykoath_append_tlv(body, sizeof(body), &body_len, YKOATH_TAG_CHALLENGE, host_chal, sizeof(host_chal))) {
        memset(key, 0, sizeof(key));
        return YkoathErrorNoSpace;
    }

    uint8_t apdu[5 + 128];
    apdu[0] = 0x00;
    apdu[1] = 0xA3;
    apdu[2] = 0x00;
    apdu[3] = 0x00;
    apdu[4] = (uint8_t)body_len;
    memcpy(apdu + 5, body, body_len);

    uint8_t resp[YKOATH_RESP_MAX];
    size_t resp_len = 0;
    uint16_t sw = 0;
    YkoathResult r =
        ykoath_transceive(poller, apdu, 5 + body_len, resp, sizeof(resp), &resp_len, &sw);
    if(r != YkoathOk) {
        memset(key, 0, sizeof(key));
        return r;
    }
    if(sw != 0x9000) {
        memset(key, 0, sizeof(key));
        FURI_LOG_E(TAG, "VALIDATE SW=%04X", sw);
        return YkoathErrorAuth;
    }

    /* Mutual auth: card response must match HMAC(host_chal, key). */
    if(resp_len < 2 || resp[0] != YKOATH_TAG_RESPONSE) {
        memset(key, 0, sizeof(key));
        return YkoathErrorParse;
    }
    size_t offs = 1;
    size_t tlen = 0;
    if(!ykoath_read_tlv_len(resp, resp_len, &offs, &tlen) || offs + tlen > resp_len) {
        memset(key, 0, sizeof(key));
        return YkoathErrorParse;
    }
    uint8_t expect[CRYPTO_SHA1_SIZE];
    if(!crypto_hmac_sha1(key, sizeof(key), host_chal, sizeof(host_chal), expect)) {
        memset(key, 0, sizeof(key));
        return YkoathErrorAuth;
    }
    bool ok = (tlen == CRYPTO_SHA1_SIZE) && (memcmp(resp + offs, expect, CRYPTO_SHA1_SIZE) == 0);
    memset(key, 0, sizeof(key));
    memset(host_resp, 0, sizeof(host_resp));
    memset(expect, 0, sizeof(expect));
    return ok ? YkoathOk : YkoathErrorAuth;
}

static YkoathResult ykoath_parse_calculate_all(const uint8_t* data, size_t len, YkoathCredentialList* list) {
    list->count = 0;
    size_t offs = 0;
    while(offs + 1 < len && list->count < YKOATH_CRED_MAX) {
        if(data[offs++] != YKOATH_TAG_NAME) return YkoathErrorParse;
        size_t name_len = 0;
        if(!ykoath_read_tlv_len(data, len, &offs, &name_len) || offs + name_len > len) {
            return YkoathErrorParse;
        }
        char name[YKOATH_NAME_MAX];
        size_t copy = name_len;
        if(copy >= YKOATH_NAME_MAX) copy = YKOATH_NAME_MAX - 1;
        memcpy(name, data + offs, copy);
        name[copy] = '\0';
        offs += name_len;

        if(offs >= len) return YkoathErrorParse;
        uint8_t rtag = data[offs++];
        size_t rlen = 0;
        if(!ykoath_read_tlv_len(data, len, &offs, &rlen) || offs + rlen > len) {
            return YkoathErrorParse;
        }

        if(rtag == YKOATH_TAG_NO_RESPONSE || rtag == YKOATH_TAG_PROPERTY_TOUCH) {
            /* Skip HOTP / touch — omit from UI list. */
            offs += rlen;
            continue;
        }

        if(rtag != YKOATH_TAG_T_RESPONSE && rtag != YKOATH_TAG_RESPONSE) {
            return YkoathErrorParse;
        }
        if(rlen < 5) return YkoathErrorParse; /* digits + 4-byte code */

        YkoathCredential* cred = &list->items[list->count];
        memset(cred, 0, sizeof(*cred));
        strncpy(cred->name, name, sizeof(cred->name) - 1);
        cred->digits = data[offs];
        ykoath_format_code(cred->digits, data + offs + 1, cred->code, sizeof(cred->code));
        list->count++;
        offs += rlen;
    }
    return YkoathOk;
}

YkoathResult ykoath_calculate_all(
    Iso14443_4aPoller* poller,
    uint64_t unix_time,
    YkoathCredentialList* list) {
    uint64_t counter = unix_time / 30;
    uint8_t challenge[8];
    for(int i = 7; i >= 0; i--) {
        challenge[i] = (uint8_t)(counter & 0xff);
        counter >>= 8;
    }

    uint8_t body[16];
    size_t body_len = 0;
    if(!ykoath_append_tlv(body, sizeof(body), &body_len, YKOATH_TAG_CHALLENGE, challenge, sizeof(challenge))) {
        return YkoathErrorNoSpace;
    }

    uint8_t apdu[5 + 16];
    apdu[0] = 0x00;
    apdu[1] = 0xA4; /* CALCULATE ALL */
    apdu[2] = 0x00;
    apdu[3] = 0x01; /* truncated */
    apdu[4] = (uint8_t)body_len;
    memcpy(apdu + 5, body, body_len);

    uint8_t resp[YKOATH_RESP_MAX];
    size_t resp_len = 0;
    uint16_t sw = 0;
    YkoathResult r =
        ykoath_transceive(poller, apdu, 5 + body_len, resp, sizeof(resp), &resp_len, &sw);
    if(r != YkoathOk) return r;
    if(sw != 0x9000) {
        FURI_LOG_E(TAG, "CALCULATE ALL SW=%04X", sw);
        return YkoathErrorSw;
    }
    return ykoath_parse_calculate_all(resp, resp_len, list);
}

YkoathResult ykoath_open(
    Iso14443_4aPoller* poller,
    const char* password_or_null,
    YkoathSelectInfo* select_out) {
    YkoathSelectInfo select;
    YkoathResult r = ykoath_select(poller, &select);
    if(r != YkoathOk) return r;
    if(select_out) *select_out = select;

    if(select.has_challenge) {
        if(!password_or_null || password_or_null[0] == '\0') {
            return YkoathErrorNeedPin;
        }
        r = ykoath_validate(poller, &select, password_or_null);
        if(r != YkoathOk) return r;
    }
    return YkoathOk;
}

static void ykoath_time_challenge(uint64_t unix_time, uint8_t challenge[8]) {
    uint64_t counter = unix_time / 30;
    for(int i = 7; i >= 0; i--) {
        challenge[i] = (uint8_t)(counter & 0xff);
        counter >>= 8;
    }
}

static YkoathResult ykoath_parse_list(const uint8_t* data, size_t len, YkoathCredentialList* list) {
    list->count = 0;
    size_t offs = 0;
    while(offs + 1 < len && list->count < YKOATH_CRED_MAX) {
        if(data[offs++] != YKOATH_TAG_NAME_LIST) return YkoathErrorParse;
        size_t tlen = 0;
        if(!ykoath_read_tlv_len(data, len, &offs, &tlen) || offs + tlen > len || tlen < 1) {
            return YkoathErrorParse;
        }
        YkoathCredential* cred = &list->items[list->count];
        memset(cred, 0, sizeof(*cred));
        cred->algo = data[offs];
        size_t name_len = tlen - 1;
        if(name_len >= YKOATH_NAME_MAX) name_len = YKOATH_NAME_MAX - 1;
        memcpy(cred->name, data + offs + 1, name_len);
        cred->name[name_len] = '\0';
        /* Skip HOTP in manage list display? Keep them for delete/calculate choice. */
        list->count++;
        offs += tlen;
    }
    return YkoathOk;
}

YkoathResult ykoath_list(Iso14443_4aPoller* poller, YkoathCredentialList* list) {
    uint8_t apdu[5] = {0x00, 0xA1, 0x00, 0x00, 0x00};
    uint8_t resp[YKOATH_RESP_MAX];
    size_t resp_len = 0;
    uint16_t sw = 0;
    YkoathResult r = ykoath_transceive(poller, apdu, sizeof(apdu), resp, sizeof(resp), &resp_len, &sw);
    if(r != YkoathOk) return r;
    if(sw != 0x9000) {
        FURI_LOG_E(TAG, "LIST SW=%04X", sw);
        return (sw == 0x6982) ? YkoathErrorAuth : YkoathErrorSw;
    }
    return ykoath_parse_list(resp, resp_len, list);
}

YkoathResult ykoath_calculate(
    Iso14443_4aPoller* poller,
    const char* name,
    uint64_t unix_time,
    YkoathCredential* out) {
    furi_check(name && out);
    size_t name_len = strlen(name);
    if(name_len == 0 || name_len >= YKOATH_NAME_MAX) return YkoathErrorSyntax;

    uint8_t challenge[8];
    ykoath_time_challenge(unix_time, challenge);

    uint8_t body[2 + YKOATH_NAME_MAX + 2 + 8];
    size_t body_len = 0;
    if(!ykoath_append_tlv(
           body, sizeof(body), &body_len, YKOATH_TAG_NAME, (const uint8_t*)name, name_len)) {
        return YkoathErrorNoSpace;
    }
    if(!ykoath_append_tlv(body, sizeof(body), &body_len, YKOATH_TAG_CHALLENGE, challenge, 8)) {
        return YkoathErrorNoSpace;
    }

    uint8_t apdu[5 + sizeof(body)];
    apdu[0] = 0x00;
    apdu[1] = 0xA2; /* CALCULATE */
    apdu[2] = 0x00;
    apdu[3] = 0x01; /* truncated */
    apdu[4] = (uint8_t)body_len;
    memcpy(apdu + 5, body, body_len);

    uint8_t resp[YKOATH_RESP_MAX];
    size_t resp_len = 0;
    uint16_t sw = 0;
    YkoathResult r =
        ykoath_transceive(poller, apdu, 5 + body_len, resp, sizeof(resp), &resp_len, &sw);
    if(r != YkoathOk) return r;
    if(sw == 0x6984) return YkoathErrorNotFound;
    if(sw != 0x9000) {
        FURI_LOG_E(TAG, "CALCULATE SW=%04X", sw);
        return YkoathErrorSw;
    }
    if(resp_len < 7) return YkoathErrorParse;
    if(resp[0] != YKOATH_TAG_T_RESPONSE && resp[0] != YKOATH_TAG_RESPONSE) {
        return YkoathErrorParse;
    }
    size_t offs = 1;
    size_t tlen = 0;
    if(!ykoath_read_tlv_len(resp, resp_len, &offs, &tlen) || offs + tlen > resp_len || tlen < 5) {
        return YkoathErrorParse;
    }

    memset(out, 0, sizeof(*out));
    strncpy(out->name, name, sizeof(out->name) - 1);
    out->digits = resp[offs];
    ykoath_format_code(out->digits, resp + offs + 1, out->code, sizeof(out->code));
    return YkoathOk;
}

YkoathResult ykoath_put(Iso14443_4aPoller* poller, const YkoathPutParams* params) {
    furi_check(params);
    size_t name_len = strlen(params->name);
    if(name_len == 0 || name_len >= YKOATH_NAME_MAX) return YkoathErrorSyntax;
    if(params->key_len == 0 || params->key_len > YKOATH_KEY_MAX) return YkoathErrorSyntax;
    if(params->digits < 1 || params->digits > 9) return YkoathErrorSyntax;

    uint8_t key_tlv[2 + YKOATH_KEY_MAX];
    key_tlv[0] = params->type_alg ? params->type_alg : (YKOATH_TYPE_TOTP | YKOATH_ALG_SHA1);
    key_tlv[1] = params->digits;
    memcpy(key_tlv + 2, params->key, params->key_len);
    size_t key_tlv_len = 2 + params->key_len;

    uint8_t body[2 + YKOATH_NAME_MAX + 2 + sizeof(key_tlv)];
    size_t body_len = 0;
    if(!ykoath_append_tlv(
           body, sizeof(body), &body_len, YKOATH_TAG_NAME, (const uint8_t*)params->name, name_len)) {
        return YkoathErrorNoSpace;
    }
    if(!ykoath_append_tlv(body, sizeof(body), &body_len, YKOATH_TAG_KEY, key_tlv, key_tlv_len)) {
        return YkoathErrorNoSpace;
    }

    uint8_t apdu[5 + sizeof(body)];
    apdu[0] = 0x00;
    apdu[1] = 0x01; /* PUT */
    apdu[2] = 0x00;
    apdu[3] = 0x00;
    apdu[4] = (uint8_t)body_len;
    memcpy(apdu + 5, body, body_len);

    uint8_t resp[YKOATH_RESP_MAX];
    size_t resp_len = 0;
    uint16_t sw = 0;
    YkoathResult r =
        ykoath_transceive(poller, apdu, 5 + body_len, resp, sizeof(resp), &resp_len, &sw);
    if(r != YkoathOk) return r;
    if(sw == 0x6A84) return YkoathErrorNoSpace;
    if(sw == 0x6A80) return YkoathErrorSyntax;
    if(sw != 0x9000) {
        FURI_LOG_E(TAG, "PUT SW=%04X", sw);
        return YkoathErrorSw;
    }
    return YkoathOk;
}

YkoathResult ykoath_delete(Iso14443_4aPoller* poller, const char* name) {
    furi_check(name);
    size_t name_len = strlen(name);
    if(name_len == 0 || name_len >= YKOATH_NAME_MAX) return YkoathErrorSyntax;

    uint8_t body[2 + YKOATH_NAME_MAX];
    size_t body_len = 0;
    if(!ykoath_append_tlv(
           body, sizeof(body), &body_len, YKOATH_TAG_NAME, (const uint8_t*)name, name_len)) {
        return YkoathErrorNoSpace;
    }

    uint8_t apdu[5 + sizeof(body)];
    apdu[0] = 0x00;
    apdu[1] = 0x02; /* DELETE */
    apdu[2] = 0x00;
    apdu[3] = 0x00;
    apdu[4] = (uint8_t)body_len;
    memcpy(apdu + 5, body, body_len);

    uint8_t resp[YKOATH_RESP_MAX];
    size_t resp_len = 0;
    uint16_t sw = 0;
    YkoathResult r =
        ykoath_transceive(poller, apdu, 5 + body_len, resp, sizeof(resp), &resp_len, &sw);
    if(r != YkoathOk) return r;
    if(sw == 0x6984) return YkoathErrorNotFound;
    if(sw != 0x9000) {
        FURI_LOG_E(TAG, "DELETE SW=%04X", sw);
        return YkoathErrorSw;
    }
    return YkoathOk;
}

YkoathResult ykoath_read_totp(
    Iso14443_4aPoller* poller,
    const char* password_or_null,
    uint64_t unix_time,
    YkoathSelectInfo* select_out,
    YkoathCredentialList* list) {
    YkoathResult r = ykoath_open(poller, password_or_null, select_out);
    if(r != YkoathOk) return r;
    return ykoath_calculate_all(poller, unix_time, list);
}

static int ykoath_base32_val(char c) {
    if(c >= 'A' && c <= 'Z') return c - 'A';
    if(c >= 'a' && c <= 'z') return c - 'a';
    if(c >= '2' && c <= '7') return c - '2' + 26;
    return -1;
}

static bool ykoath_is_hex_char(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int ykoath_hex_val(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool ykoath_parse_secret(const char* text, uint8_t* out, size_t out_cap, size_t* out_len) {
    if(!text || !out || !out_len || out_cap == 0) return false;
    *out_len = 0;

    /* Prefer hex if every non-space char is hex and we have an even count. */
    size_t hex_digits = 0;
    bool only_hex = true;
    for(const char* p = text; *p; p++) {
        if(*p == ' ' || *p == '\t') continue;
        if(!ykoath_is_hex_char(*p)) {
            only_hex = false;
            break;
        }
        hex_digits++;
    }
    if(only_hex && hex_digits >= 2 && (hex_digits % 2) == 0) {
        size_t o = 0;
        int hi = -1;
        for(const char* p = text; *p; p++) {
            if(*p == ' ' || *p == '\t') continue;
            int v = ykoath_hex_val(*p);
            if(v < 0) return false;
            if(hi < 0) {
                hi = v;
            } else {
                if(o >= out_cap) return false;
                out[o++] = (uint8_t)((hi << 4) | v);
                hi = -1;
            }
        }
        *out_len = o;
        return o > 0;
    }

    /* Base32 (ignore spaces and '=') */
    uint32_t buffer = 0;
    int bits_left = 0;
    size_t o = 0;
    for(const char* p = text; *p; p++) {
        if(*p == ' ' || *p == '\t' || *p == '=') continue;
        int v = ykoath_base32_val(*p);
        if(v < 0) return false;
        buffer = (buffer << 5) | (uint32_t)v;
        bits_left += 5;
        if(bits_left >= 8) {
            bits_left -= 8;
            if(o >= out_cap) return false;
            out[o++] = (uint8_t)((buffer >> bits_left) & 0xff);
        }
    }
    *out_len = o;
    return o > 0;
}

const char* ykoath_result_str(YkoathResult result) {
    switch(result) {
    case YkoathOk:
        return "OK";
    case YkoathErrorTransport:
        return "NFC transport error";
    case YkoathErrorSw:
        return "Card rejected command";
    case YkoathErrorParse:
        return "Bad card response";
    case YkoathErrorAuth:
        return "OATH password failed";
    case YkoathErrorNoSpace:
        return "No space / too large";
    case YkoathErrorNeedPin:
        return "OATH password required";
    case YkoathErrorNotFound:
        return "Credential not found";
    case YkoathErrorSyntax:
        return "Bad name or secret";
    case YkoathErrorNoApplet:
        return "No OATH applet";
    default:
        return "Unknown error";
    }
}
