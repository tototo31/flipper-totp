#include "hid_type.h"

#include <furi.h>
#include <furi_hal_usb.h>
#include <furi_hal_usb_hid.h>
#include <stdio.h>

#define TAG "TotpHid"

/*
 * Host re-enumeration after CDC→HID often takes several seconds (esp. Linux).
 * sequence_error is a *double* beep — callers must not play it while waiting
 * or merely because CDC dropped; only success after keys are actually sent.
 */
#define TOTP_HID_KEY_DELAY_MS 25
#define TOTP_HID_KEY_RETRY 6
#define TOTP_HID_CONNECT_POLL_MS 50
#define TOTP_HID_CONNECT_TIMEOUT_MS 15000
#define TOTP_HID_POST_SWITCH_MS 800
#define TOTP_HID_POST_CONNECT_MS 750
#define TOTP_HID_PRE_RESTORE_MS 250
/* After this, try probing key reports even if is_connected is still false. */
#define TOTP_HID_PROBE_AFTER_MS 2500

static volatile bool totp_hid_cancel;

void totp_hid_type_request_cancel(void) {
    totp_hid_cancel = true;
}

void totp_hid_type_clear_cancel(void) {
    totp_hid_cancel = false;
}

bool totp_hid_type_is_cancelled(void) {
    return totp_hid_cancel;
}

static void totp_hid_status(TotpHidTypeStatusCb cb, void* ctx, const char* msg) {
    if(cb) cb(msg, ctx);
}

static bool totp_hid_code_is_digits(const char* digits) {
    if(!digits || !digits[0]) return false;
    for(const char* p = digits; *p; p++) {
        if(*p < '0' || *p > '9') return false;
    }
    return true;
}

/** Sleep in short chunks so cancel is noticed during hold/gap. */
static bool totp_hid_delay_cancelable(uint32_t ms) {
    while(ms > 0) {
        if(totp_hid_cancel) return false;
        uint32_t chunk = (ms > 25) ? 25 : ms;
        furi_delay_ms(chunk);
        ms -= chunk;
    }
    return !totp_hid_cancel;
}

typedef struct {
    FuriSemaphore* sem;
    volatile bool connected;
} TotpHidWaitCtx;

static void totp_hid_state_cb(bool state, void* context) {
    TotpHidWaitCtx* wait = context;
    wait->connected = state;
    if(state && wait->sem) {
        furi_semaphore_release(wait->sem);
    }
}

static bool totp_hid_type_key(uint16_t key) {
    if(key == HID_KEYBOARD_NONE || totp_hid_cancel) return false;
    for(uint8_t attempt = 0; attempt < TOTP_HID_KEY_RETRY; attempt++) {
        if(totp_hid_cancel) return false;
        if(furi_hal_hid_kb_press(key)) {
            if(!totp_hid_delay_cancelable(TOTP_HID_KEY_DELAY_MS)) {
                furi_hal_hid_kb_release_all();
                return false;
            }
            if(furi_hal_hid_kb_release(key)) {
                if(!totp_hid_delay_cancelable(TOTP_HID_KEY_DELAY_MS)) {
                    furi_hal_hid_kb_release_all();
                    return false;
                }
                return true;
            }
            furi_hal_hid_kb_release_all();
            if(!totp_hid_delay_cancelable(TOTP_HID_KEY_DELAY_MS)) return false;
        } else {
            if(!totp_hid_delay_cancelable(TOTP_HID_KEY_DELAY_MS * 2)) return false;
        }
    }
    return false;
}

/** True if host accepted at least one HID keyboard report. */
static bool totp_hid_probe_ready(void) {
    /* release_all is a real IN report; success means the host is polling HID. */
    return furi_hal_hid_kb_release_all();
}

/**
 * Wait until HID is host-ready. Never notifies — caller owns UI status.
 * CDC dropping during the preceding mode switch is expected.
 * Returns false on timeout or cancel (check totp_hid_type_is_cancelled).
 */
static bool totp_hid_wait_ready(uint32_t timeout_ms, TotpHidTypeStatusCb cb, void* ctx) {
    TotpHidWaitCtx wait = {
        .sem = furi_semaphore_alloc(1, 0),
        .connected = furi_hal_hid_is_connected(),
    };
    furi_hal_hid_set_state_callback(totp_hid_state_cb, &wait);

    if(wait.connected) {
        furi_semaphore_release(wait.sem);
    }

    uint32_t waited = 0;
    bool ready = false;

    while(waited < timeout_ms) {
        if(totp_hid_cancel) {
            FURI_LOG_I(TAG, "HID wait cancelled after %lu ms", (unsigned long)waited);
            break;
        }

        if(furi_hal_hid_is_connected() || wait.connected) {
            totp_hid_status(cb, ctx, "Host ready…");
            if(!totp_hid_delay_cancelable(TOTP_HID_POST_CONNECT_MS)) break;
            ready = !totp_hid_cancel;
            break;
        }

        /* Some hosts are slow to set the connected flag; probe reports. */
        if(waited >= TOTP_HID_PROBE_AFTER_MS && totp_hid_probe_ready()) {
            FURI_LOG_I(TAG, "HID ready via report probe after %lu ms", (unsigned long)waited);
            totp_hid_status(cb, ctx, "Host ready…");
            if(!totp_hid_delay_cancelable(TOTP_HID_POST_CONNECT_MS)) break;
            ready = !totp_hid_cancel;
            break;
        }

        if((waited % 1000) == 0) {
            char line[32];
            snprintf(
                line,
                sizeof(line),
                "Waiting host… %lus",
                (unsigned long)(waited / 1000));
            totp_hid_status(cb, ctx, line);
        }

        FuriStatus st = furi_semaphore_acquire(wait.sem, TOTP_HID_CONNECT_POLL_MS);
        UNUSED(st);
        waited += TOTP_HID_CONNECT_POLL_MS;
    }

    furi_hal_hid_set_state_callback(NULL, NULL);
    furi_semaphore_free(wait.sem);

    if(totp_hid_cancel) return false;

    if(!ready && (furi_hal_hid_is_connected() || totp_hid_probe_ready())) {
        if(totp_hid_delay_cancelable(TOTP_HID_POST_CONNECT_MS)) {
            ready = true;
        }
    }
    return ready && !totp_hid_cancel;
}

TotpHidTypeResult totp_hid_type_digits(
    const char* digits,
    bool press_enter,
    TotpHidTypeStatusCb status_cb,
    void* status_ctx) {
    if(!totp_hid_code_is_digits(digits)) {
        return TotpHidTypeBadCode;
    }
    if(totp_hid_cancel) {
        return TotpHidTypeCancelled;
    }

    FuriHalUsbInterface* prev = furi_hal_usb_get_config();
    bool switched = false;
    TotpHidTypeResult result = TotpHidTypeTimeout;

    if(prev != &usb_hid) {
        totp_hid_status(status_cb, status_ctx, "Switching USB…");
        furi_hal_usb_unlock();
        /* CDC / ufbt serial drops here — expected, not a failure. */
        if(!furi_hal_usb_set_config(&usb_hid, NULL)) {
            FURI_LOG_E(TAG, "usb_hid set_config failed");
            return TotpHidTypeUsbBusy;
        }
        switched = true;
        if(!totp_hid_delay_cancelable(TOTP_HID_POST_SWITCH_MS)) {
            result = TotpHidTypeCancelled;
            goto restore;
        }
    }

    if(totp_hid_cancel) {
        result = TotpHidTypeCancelled;
        goto restore;
    }

    totp_hid_status(status_cb, status_ctx, "Waiting host… 0s");
    if(!totp_hid_wait_ready(TOTP_HID_CONNECT_TIMEOUT_MS, status_cb, status_ctx)) {
        if(totp_hid_cancel) {
            FURI_LOG_I(TAG, "HID type cancelled while waiting");
            result = TotpHidTypeCancelled;
        } else {
            FURI_LOG_W(TAG, "HID not ready after %u ms", TOTP_HID_CONNECT_TIMEOUT_MS);
            result = TotpHidTypeTimeout;
        }
        goto restore;
    }

    totp_hid_status(status_cb, status_ctx, "Typing…");
    result = TotpHidTypeOk;
    for(const char* p = digits; *p; p++) {
        if(totp_hid_cancel) {
            result = TotpHidTypeCancelled;
            break;
        }
        if(!totp_hid_type_key(HID_ASCII_TO_KEY(*p))) {
            if(totp_hid_cancel) {
                result = TotpHidTypeCancelled;
            } else {
                FURI_LOG_E(TAG, "key send failed for '%c'", *p);
                result = TotpHidTypeSendFailed;
            }
            break;
        }
    }

    if(result == TotpHidTypeOk && press_enter) {
        if(!totp_hid_type_key(HID_KEYBOARD_RETURN)) {
            if(totp_hid_cancel) {
                result = TotpHidTypeCancelled;
            } else {
                FURI_LOG_E(TAG, "Enter send failed");
                result = TotpHidTypeSendFailed;
            }
        }
    }

    furi_hal_hid_kb_release_all();
    /* Let the last report flush before tearing HID down. */
    if(result != TotpHidTypeCancelled) {
        totp_hid_delay_cancelable(TOTP_HID_PRE_RESTORE_MS);
    }

restore:
    if(switched) {
        totp_hid_status(status_cb, status_ctx, "Restoring USB…");
        furi_hal_hid_set_state_callback(NULL, NULL);
        furi_hal_usb_unlock();
        if(!furi_hal_usb_set_config(prev, NULL)) {
            FURI_LOG_W(TAG, "restore previous USB config failed");
        }
        /* Host re-enumerates CDC; do not treat that as Type failure. */
        furi_delay_ms(TOTP_HID_POST_SWITCH_MS);
    }
    return result;
}
