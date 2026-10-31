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

/* Per-exchange ISO14443-4 payload; short APDU data ≤256 + SW. */
#define YKOATH_TX_CAP 512
#define YKOATH_RX_CAP 512
#define YKOATH_TX_RETRIES 4
#define YKOATH_TX_RETRY_DELAY_MS 15
/* OATH SEND REMAINING (not ISO7816 GET RESPONSE 0xC0). */
#define YKOATH_INS_SEND_REMAINING 0xA5
#define YKOATH_SEND_REMAINING_MAX 32
/*
 * Reader FSDI=256 ⇒ max frame 256 incl. PCB+CRC. Ne=256 (Le=0) makes the card
 * return a body that needs ISO-DEP I-block chaining; Momentum's poller does not
 * reassemble card→reader chains (Protocol / short tail → missing creds 1..N).
 * Cap Le so each APDU chunk fits in one I-block; loop SEND REMAINING for the rest.
 */
#define YKOATH_APDU_LE 0x80u

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

/** Le for SEND REMAINING: never 0/256 (FSD overflow); never ask past SW2 when SW2>0. */
static uint8_t ykoath_remaining_le(uint16_t sw) {
    uint8_t sw2 = (uint8_t)(sw & 0xFF);
    if(sw2 == 0) return YKOATH_APDU_LE; /* 61 00 ⇒ ≥256 left; take a safe chunk */
    return (sw2 < YKOATH_APDU_LE) ? sw2 : (uint8_t)YKOATH_APDU_LE;
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
        /*
         * Protocol after a large reply usually means lost ISO-DEP chaining.
         * Retrying the same APDU while the card still expects an R-ACK yields
         * a short tail (e.g. only creds 24–32). Bail out so NfcSession Reset
         * re-activates the card instead.
         */
        if(err == Iso14443_4aErrorProtocol) {
            break;
        }
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
        FURI_LOG_E(
            TAG,
            "APDU INS=%02X chunk %u > cap %u",
            apdu_len > 1 ? apdu[1] : 0,
            (unsigned)data_len,
            (unsigned)out_cap);
        bit_buffer_free(tx);
        bit_buffer_free(rx);
        return YkoathErrorNoSpace;
    }
    memcpy(out, rx_data, data_len);
    *out_len = data_len;
    *sw = ((uint16_t)rx_data[rx_len - 2] << 8) | rx_data[rx_len - 1];
    FURI_LOG_D(
        TAG,
        "APDU INS=%02X first chunk %u SW=%04X",
        apdu_len > 1 ? apdu[1] : 0,
        (unsigned)*out_len,
        *sw);

    /* OATH applet: INS 0xA5 SEND REMAINING while SW1=0x61 (SW2 = bytes left). */
    uint8_t rem_rounds = 0;
    while((*sw & 0xFF00) == 0x6100) {
        if(++rem_rounds > YKOATH_SEND_REMAINING_MAX) {
            FURI_LOG_E(TAG, "SEND REMAINING exceeded %u rounds (len=%u)", rem_rounds, (unsigned)*out_len);
            bit_buffer_free(tx);
            bit_buffer_free(rx);
            return YkoathErrorNoSpace;
        }
        uint8_t le = ykoath_remaining_le(*sw);
        uint8_t rem_apdu[5] = {0x00, YKOATH_INS_SEND_REMAINING, 0x00, 0x00, le};
        FURI_LOG_I(
            TAG,
            "SEND REMAINING #%u SW=%04X Le=%02X have=%u",
            rem_rounds,
            *sw,
            le,
            (unsigned)*out_len);
        err = Iso14443_4aErrorTimeout;
        for(uint8_t attempt = 0; attempt < YKOATH_TX_RETRIES; attempt++) {
            bit_buffer_reset(tx);
            bit_buffer_reset(rx);
            bit_buffer_copy_bytes(tx, rem_apdu, sizeof(rem_apdu));
            err = ykoath_send_block(poller, tx, rx);
            if(err == Iso14443_4aErrorNone && bit_buffer_get_size_bytes(rx) >= 2) {
                break;
            }
            if(err == Iso14443_4aErrorProtocol) {
                break;
            }
            furi_delay_ms(YKOATH_TX_RETRY_DELAY_MS);
        }
        if(err != Iso14443_4aErrorNone || bit_buffer_get_size_bytes(rx) < 2) {
            FURI_LOG_E(TAG, "SEND REMAINING failed: err=%d", err);
            bit_buffer_free(tx);
            bit_buffer_free(rx);
            return YkoathErrorTransport;
        }
        rx_len = bit_buffer_get_size_bytes(rx);
        rx_data = bit_buffer_get_data(rx);
        data_len = rx_len - 2;
        if(*out_len + data_len > out_cap) {
            FURI_LOG_E(
                TAG,
                "APDU resp overflow: have=%u +=%u cap=%u (SW was %04X)",
                (unsigned)*out_len,
                (unsigned)data_len,
                (unsigned)out_cap,
                *sw);
            bit_buffer_free(tx);
            bit_buffer_free(rx);
            return YkoathErrorNoSpace;
        }
        memcpy(out + *out_len, rx_data, data_len);
        *out_len += data_len;
        *sw = ((uint16_t)rx_data[rx_len - 2] << 8) | rx_data[rx_len - 1];
    }

    FURI_LOG_I(
        TAG,
        "APDU INS=%02X complete len=%u SW=%04X rounds=%u",
        apdu_len > 1 ? apdu[1] : 0,
        (unsigned)*out_len,
        *sw,
        rem_rounds);
    bit_buffer_free(tx);
    bit_buffer_free(rx);
    return YkoathOk;
}

/** Heap buffer for large LIST / CALCULATE ALL bodies (FAP stack is small). */
static uint8_t* ykoath_resp_alloc(void) {
    return malloc(YKOATH_RESP_MAX);
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

    uint8_t resp[256];
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

    uint8_t resp[256];
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
        uint8_t name_tag = data[offs++];
        if(name_tag != YKOATH_TAG_NAME) {
            FURI_LOG_E(
                TAG,
                "calc-all parse: expected NAME at offs=%u/%u got %02X (count=%u)",
                (unsigned)(offs - 1),
                (unsigned)len,
                name_tag,
                (unsigned)list->count);
            return YkoathErrorParse;
        }
        size_t name_len = 0;
        if(!ykoath_read_tlv_len(data, len, &offs, &name_len) || offs + name_len > len) {
            FURI_LOG_E(
                TAG,
                "calc-all parse: truncated NAME TLV offs=%u len=%u (count=%u)",
                (unsigned)offs,
                (unsigned)len,
                (unsigned)list->count);
            return YkoathErrorParse;
        }
        char name[YKOATH_NAME_MAX];
        size_t copy = name_len;
        if(copy >= YKOATH_NAME_MAX) copy = YKOATH_NAME_MAX - 1;
        memcpy(name, data + offs, copy);
        name[copy] = '\0';
        offs += name_len;

        if(offs >= len) {
            FURI_LOG_E(
                TAG,
                "calc-all parse: missing response TLV after '%s' (offs=%u len=%u)",
                name,
                (unsigned)offs,
                (unsigned)len);
            return YkoathErrorParse;
        }
        uint8_t rtag = data[offs++];
        size_t rlen = 0;
        if(!ykoath_read_tlv_len(data, len, &offs, &rlen) || offs + rlen > len) {
            FURI_LOG_E(
                TAG,
                "calc-all parse: truncated resp TLV tag=%02X after '%s' (offs=%u len=%u)",
                rtag,
                name,
                (unsigned)offs,
                (unsigned)len);
            return YkoathErrorParse;
        }

        if(rtag == YKOATH_TAG_NO_RESPONSE || rtag == YKOATH_TAG_PROPERTY_TOUCH) {
            /* Skip HOTP / touch — omit from UI list. */
            offs += rlen;
            continue;
        }

        if(rtag != YKOATH_TAG_T_RESPONSE && rtag != YKOATH_TAG_RESPONSE) {
            FURI_LOG_E(TAG, "calc-all parse: bad resp tag %02X for '%s'", rtag, name);
            return YkoathErrorParse;
        }
        if(rlen < 5) {
            FURI_LOG_E(TAG, "calc-all parse: short code TLV len=%u for '%s'", (unsigned)rlen, name);
            return YkoathErrorParse; /* digits + 4-byte code */
        }

        YkoathCredential* cred = &list->items[list->count];
        memset(cred, 0, sizeof(*cred));
        strncpy(cred->name, name, sizeof(cred->name) - 1);
        cred->digits = data[offs];
        ykoath_format_code(cred->digits, data + offs + 1, cred->code, sizeof(cred->code));
        list->count++;
        offs += rlen;
    }
    if(offs < len && list->count >= YKOATH_CRED_MAX) {
        FURI_LOG_W(
            TAG,
            "calc-all: hit CRED_MAX=%u with %u bytes left",
            (unsigned)YKOATH_CRED_MAX,
            (unsigned)(len - offs));
    } else if(offs != len && list->count > 0) {
        /* Trailing bytes after a full parse are unexpected but non-fatal if aligned. */
        FURI_LOG_W(TAG, "calc-all: %u trailing bytes after %u creds", (unsigned)(len - offs), (unsigned)list->count);
    }
    FURI_LOG_I(TAG, "calc-all parsed %u credentials (%u byte body)", (unsigned)list->count, (unsigned)len);
    return YkoathOk;
}

/**
 * LIST then per-credential CALCULATE — slower over NFC but avoids one huge
 * CALCULATE ALL body when SEND REMAINING / buffer / parse fails.
 * Reuses @p list in place (LIST names → overwrite with codes) to save stack.
 */
static YkoathResult ykoath_calculate_all_via_list(
    Iso14443_4aPoller* poller,
    uint64_t unix_time,
    YkoathCredentialList* list) {
    YkoathResult r = ykoath_list(poller, list);
    if(r != YkoathOk) return r;

    size_t n = list->count;
    size_t out = 0;
    for(size_t i = 0; i < n && out < YKOATH_CRED_MAX; i++) {
        char name[YKOATH_NAME_MAX];
        uint8_t algo = list->items[i].algo;
        strncpy(name, list->items[i].name, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';

        /* LIST algo high nibble: 0x10 HOTP / 0x20 TOTP. Skip HOTP (counter bump). */
        if((algo & 0xF0) == YKOATH_TYPE_HOTP) continue;
        if(name[0] == '\0') continue;

        YkoathCredential cred;
        r = ykoath_calculate(poller, name, unix_time, &cred);
        if(r == YkoathErrorNotFound) continue;
        if(r != YkoathOk) {
            FURI_LOG_E(TAG, "fallback CALCULATE '%s' failed: %d", name, r);
            list->count = out;
            return r;
        }
        list->items[out++] = cred;
    }
    list->count = out;
    FURI_LOG_I(TAG, "fallback LIST+CALCULATE got %u codes", (unsigned)list->count);
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

    /* Case 4: Le capped under FSD so the card uses 61xx instead of ISO chaining. */
    uint8_t apdu[5 + 16 + 1];
    apdu[0] = 0x00;
    apdu[1] = 0xA4; /* CALCULATE ALL */
    apdu[2] = 0x00;
    apdu[3] = 0x01; /* truncated */
    apdu[4] = (uint8_t)body_len;
    memcpy(apdu + 5, body, body_len);
    apdu[5 + body_len] = (uint8_t)YKOATH_APDU_LE;
    size_t apdu_len = 5 + body_len + 1;

    uint8_t* resp = ykoath_resp_alloc();
    if(!resp) return YkoathErrorNoSpace;
    size_t resp_len = 0;
    uint16_t sw = 0;
    YkoathResult r = ykoath_transceive(poller, apdu, apdu_len, resp, YKOATH_RESP_MAX, &resp_len, &sw);
    bool try_fallback = false;
    if(r == YkoathOk && sw == 0x9000) {
        r = ykoath_parse_calculate_all(resp, resp_len, list);
        if(r == YkoathOk) {
            free(resp);
            return YkoathOk;
        }
        FURI_LOG_W(
            TAG,
            "CALCULATE ALL parse failed (len=%u) — fallback LIST+CALCULATE",
            (unsigned)resp_len);
        try_fallback = true;
    } else if(r == YkoathErrorNoSpace || r == YkoathErrorTransport) {
        FURI_LOG_W(
            TAG,
            "CALCULATE ALL r=%d SW=%04X len=%u — fallback LIST+CALCULATE",
            r,
            sw,
            (unsigned)resp_len);
        try_fallback = true;
    } else if(r == YkoathOk) {
        FURI_LOG_E(TAG, "CALCULATE ALL SW=%04X", sw);
        free(resp);
        return (sw == 0x6982) ? YkoathErrorAuth : YkoathErrorSw;
    } else {
        free(resp);
        return r;
    }
    free(resp);

    if(!try_fallback) return r;
    return ykoath_calculate_all_via_list(poller, unix_time, list);
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
    /* Case 2: Le under FSD → 61xx + SEND REMAINING (avoids ISO-DEP chaining). */
    uint8_t apdu[5] = {0x00, 0xA1, 0x00, 0x00, (uint8_t)YKOATH_APDU_LE};
    uint8_t* resp = ykoath_resp_alloc();
    if(!resp) return YkoathErrorNoSpace;
    size_t resp_len = 0;
    uint16_t sw = 0;
    YkoathResult r = ykoath_transceive(poller, apdu, sizeof(apdu), resp, YKOATH_RESP_MAX, &resp_len, &sw);
    if(r != YkoathOk) {
        free(resp);
        return r;
    }
    if(sw != 0x9000) {
        FURI_LOG_E(TAG, "LIST SW=%04X", sw);
        free(resp);
        return (sw == 0x6982) ? YkoathErrorAuth : YkoathErrorSw;
    }
    r = ykoath_parse_list(resp, resp_len, list);
    FURI_LOG_I(TAG, "LIST parsed %u names (%u bytes)", (unsigned)list->count, (unsigned)resp_len);
    free(resp);
    return r;
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

    uint8_t resp[64];
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

/* YubiKey / Apex reject PUT keys shorter than 14 bytes with SW=6A80.
 * yubikit zero-pads; HMAC semantics are unchanged. */
#define YKOATH_HMAC_KEY_MIN 14

YkoathResult ykoath_put(Iso14443_4aPoller* poller, const YkoathPutParams* params) {
    furi_check(params);
    size_t name_len = strlen(params->name);
    if(name_len == 0 || name_len >= YKOATH_NAME_MAX) return YkoathErrorSyntax;
    if(params->key_len == 0 || params->key_len > YKOATH_KEY_MAX) return YkoathErrorSyntax;
    if(params->digits < 1 || params->digits > 9) return YkoathErrorSyntax;

    uint8_t key_tlv[2 + YKOATH_KEY_MAX];
    key_tlv[0] = params->type_alg ? params->type_alg : (YKOATH_TYPE_TOTP | YKOATH_ALG_SHA1);
    key_tlv[1] = params->digits;
    memset(key_tlv + 2, 0, YKOATH_KEY_MAX);
    memcpy(key_tlv + 2, params->key, params->key_len);
    size_t padded_len = params->key_len;
    if(padded_len < YKOATH_HMAC_KEY_MIN) {
        padded_len = YKOATH_HMAC_KEY_MIN;
    }
    size_t key_tlv_len = 2 + padded_len;

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

    uint8_t resp[32];
    size_t resp_len = 0;
    uint16_t sw = 0;
    YkoathResult r =
        ykoath_transceive(poller, apdu, 5 + body_len, resp, sizeof(resp), &resp_len, &sw);
    if(r != YkoathOk) return r;
    if(sw == 0x6A84) return YkoathErrorNoSpace;
    if(sw == 0x6A80) {
        FURI_LOG_E(TAG, "PUT SW=6A80 (wrong data — name/key/digits)");
        return YkoathErrorSyntax;
    }
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

    uint8_t resp[32];
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

YkoathResult ykoath_set_code(
    Iso14443_4aPoller* poller,
    const YkoathSelectInfo* select,
    const char* new_password) {
    furi_check(poller && select && new_password);
    if(new_password[0] == '\0' || select->id_len == 0) return YkoathErrorSyntax;

    uint8_t key[CRYPTO_OATH_KEY_SIZE];
    if(!crypto_pbkdf2_hmac_sha1(
           (const uint8_t*)new_password,
           strlen(new_password),
           select->id,
           select->id_len,
           CRYPTO_OATH_PBKDF2_ITERS,
           key,
           sizeof(key))) {
        return YkoathErrorAuth;
    }

    uint8_t challenge[8];
    furi_hal_random_fill_buf(challenge, sizeof(challenge));

    uint8_t response[CRYPTO_SHA1_SIZE];
    if(!crypto_hmac_sha1(key, sizeof(key), challenge, sizeof(challenge), response)) {
        memset(key, 0, sizeof(key));
        return YkoathErrorAuth;
    }

    /* KEY TLV: alg byte (TOTP|SHA1 = 0x21, matches yubikit) || 16-byte key. */
    uint8_t key_tlv[1 + CRYPTO_OATH_KEY_SIZE];
    key_tlv[0] = (uint8_t)(YKOATH_TYPE_TOTP | YKOATH_ALG_SHA1);
    memcpy(key_tlv + 1, key, CRYPTO_OATH_KEY_SIZE);

    uint8_t body[96];
    size_t body_len = 0;
    if(!ykoath_append_tlv(body, sizeof(body), &body_len, YKOATH_TAG_KEY, key_tlv, sizeof(key_tlv)) ||
       !ykoath_append_tlv(
           body, sizeof(body), &body_len, YKOATH_TAG_CHALLENGE, challenge, sizeof(challenge)) ||
       !ykoath_append_tlv(
           body, sizeof(body), &body_len, YKOATH_TAG_RESPONSE, response, CRYPTO_SHA1_SIZE)) {
        memset(key, 0, sizeof(key));
        memset(key_tlv, 0, sizeof(key_tlv));
        memset(response, 0, sizeof(response));
        return YkoathErrorNoSpace;
    }

    uint8_t apdu[5 + 96];
    apdu[0] = 0x00;
    apdu[1] = 0x03; /* SET CODE */
    apdu[2] = 0x00;
    apdu[3] = 0x00;
    apdu[4] = (uint8_t)body_len;
    memcpy(apdu + 5, body, body_len);

    uint8_t resp[64];
    size_t resp_len = 0;
    uint16_t sw = 0;
    YkoathResult r =
        ykoath_transceive(poller, apdu, 5 + body_len, resp, sizeof(resp), &resp_len, &sw);

    memset(key, 0, sizeof(key));
    memset(key_tlv, 0, sizeof(key_tlv));
    memset(response, 0, sizeof(response));
    memset(body, 0, sizeof(body));

    if(r != YkoathOk) return r;
    if(sw == 0x6984) return YkoathErrorAuth; /* response doesn't match */
    if(sw == 0x6982) return YkoathErrorNeedPin;
    if(sw == 0x6A80) return YkoathErrorSyntax;
    if(sw != 0x9000) {
        FURI_LOG_E(TAG, "SET CODE SW=%04X", sw);
        return YkoathErrorSw;
    }
    return YkoathOk;
}

YkoathResult ykoath_clear_code(Iso14443_4aPoller* poller) {
    furi_check(poller);
    /* yubikit unset_key: SET CODE with empty KEY TLV (73 00), not Lc=0. */
    uint8_t body[2];
    size_t body_len = 0;
    if(!ykoath_append_tlv(body, sizeof(body), &body_len, YKOATH_TAG_KEY, NULL, 0)) {
        return YkoathErrorNoSpace;
    }

    uint8_t apdu[5 + 2];
    apdu[0] = 0x00;
    apdu[1] = 0x03; /* SET CODE */
    apdu[2] = 0x00;
    apdu[3] = 0x00;
    apdu[4] = (uint8_t)body_len;
    memcpy(apdu + 5, body, body_len);

    uint8_t resp[32];
    size_t resp_len = 0;
    uint16_t sw = 0;
    YkoathResult r =
        ykoath_transceive(poller, apdu, 5 + body_len, resp, sizeof(resp), &resp_len, &sw);
    if(r != YkoathOk) return r;
    if(sw == 0x6982) return YkoathErrorNeedPin;
    if(sw != 0x9000) {
        FURI_LOG_E(TAG, "CLEAR CODE SW=%04X", sw);
        return YkoathErrorSw;
    }
    return YkoathOk;
}

YkoathResult ykoath_reset(Iso14443_4aPoller* poller) {
    furi_check(poller);
    uint8_t apdu[4] = {0x00, 0x04, 0xDE, 0xAD};
    uint8_t resp[32];
    size_t resp_len = 0;
    uint16_t sw = 0;
    YkoathResult r = ykoath_transceive(poller, apdu, sizeof(apdu), resp, sizeof(resp), &resp_len, &sw);
    if(r != YkoathOk) return r;
    if(sw != 0x9000) {
        FURI_LOG_E(TAG, "RESET SW=%04X", sw);
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

static bool ykoath_parse_secret_hex(
    const char* text,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len) {
    size_t o = 0;
    int hi = -1;
    for(const char* p = text; *p; p++) {
        if(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') continue;
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
    if(hi >= 0) return false; /* odd nibble */
    *out_len = o;
    return o > 0;
}

static bool ykoath_parse_secret_base32(
    const char* text,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len) {
    uint32_t buffer = 0;
    int bits_left = 0;
    size_t o = 0;
    for(const char* p = text; *p; p++) {
        if(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == '=') continue;
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

bool ykoath_parse_secret(const char* text, uint8_t* out, size_t out_cap, size_t* out_len) {
    if(!text || !out || !out_len || out_cap == 0) return false;
    *out_len = 0;

    /* Skip leading whitespace / BOM-ish junk from serial. */
    while(*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') text++;

    /* Explicit hex: "hex:DEADBEEF" or "0xDEADBEEF". */
    if((text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))) {
        return ykoath_parse_secret_hex(text + 2, out, out_cap, out_len);
    }
    if((text[0] == 'h' || text[0] == 'H') && (text[1] == 'e' || text[1] == 'E') &&
       (text[2] == 'x' || text[2] == 'X') && text[3] == ':') {
        return ykoath_parse_secret_hex(text + 4, out, out_cap, out_len);
    }

    /*
     * Prefer Base32 (otpauth / Yubico default). Secrets that only use A-F and
     * digits were previously mis-read as hex (e.g. "AAAAAAAA") and the card
     * then rejected PUT with SW=6A80 → "Bad name or secret".
     */
    if(ykoath_parse_secret_base32(text, out, out_cap, out_len)) {
        return true;
    }

    /* Fallback: plain hex without prefix (even length, hex-only). */
    size_t hex_digits = 0;
    for(const char* p = text; *p; p++) {
        if(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') continue;
        if(!ykoath_is_hex_char(*p)) return false;
        hex_digits++;
    }
    if(hex_digits >= 2 && (hex_digits % 2) == 0) {
        return ykoath_parse_secret_hex(text, out, out_cap, out_len);
    }
    return false;
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
        return "PUT rejected (name/key)";
    case YkoathErrorNoApplet:
        return "No OATH applet";
    default:
        return "Unknown error";
    }
}
