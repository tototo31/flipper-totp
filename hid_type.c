#include "hid_type.h"

#include <furi.h>
#include <furi_hal_usb.h>
#include <furi_hal_usb_hid.h>

#define TAG "TotpHid"

/* Host re-enumeration after CDC→HID often takes several seconds (esp. Linux). */
#define TOTP_HID_KEY_DELAY_MS 20
#define TOTP_HID_KEY_RETRY 4
#define TOTP_HID_CONNECT_POLL_MS 50
#define TOTP_HID_CONNECT_TIMEOUT_MS 10000
#define TOTP_HID_POST_SWITCH_MS 400
#define TOTP_HID_POST_CONNECT_MS 500

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

static bool totp_hid_wait_connected(uint32_t timeout_ms) {
    /* Fast path if host already configured HID. */
    if(furi_hal_hid_is_connected()) {
        furi_delay_ms(TOTP_HID_POST_CONNECT_MS);
        return true;
    }

    TotpHidWaitCtx wait = {
        .sem = furi_semaphore_alloc(1, 0),
        .connected = false,
    };
    furi_hal_hid_set_state_callback(totp_hid_state_cb, &wait);

    /* Race: connected between poll and callback install. */
    if(furi_hal_hid_is_connected()) {
        wait.connected = true;
        furi_semaphore_release(wait.sem);
    }

    uint32_t waited = 0;
    while(!wait.connected && !furi_hal_hid_is_connected()) {
        if(waited >= timeout_ms) break;
        /* Also poll — some stacks set the flag without a fresh callback. */
        FuriStatus st = furi_semaphore_acquire(wait.sem, TOTP_HID_CONNECT_POLL_MS);
        UNUSED(st);
        waited += TOTP_HID_CONNECT_POLL_MS;
        if(furi_hal_hid_is_connected()) {
            wait.connected = true;
            break;
        }
    }

    furi_hal_hid_set_state_callback(NULL, NULL);
    furi_semaphore_free(wait.sem);

    bool ok = wait.connected || furi_hal_hid_is_connected();
    if(ok) {
        furi_delay_ms(TOTP_HID_POST_CONNECT_MS);
    }
    return ok;
}

static bool totp_hid_type_key(uint16_t key) {
    if(key == HID_KEYBOARD_NONE) return false;
    for(uint8_t attempt = 0; attempt < TOTP_HID_KEY_RETRY; attempt++) {
        if(furi_hal_hid_kb_press(key)) {
            furi_delay_ms(TOTP_HID_KEY_DELAY_MS);
            if(furi_hal_hid_kb_release(key)) {
                furi_delay_ms(TOTP_HID_KEY_DELAY_MS);
                return true;
            }
            furi_hal_hid_kb_release_all();
            furi_delay_ms(TOTP_HID_KEY_DELAY_MS);
        } else {
            furi_delay_ms(TOTP_HID_KEY_DELAY_MS * 2);
        }
    }
    return false;
}

TotpHidTypeResult totp_hid_type_digits(
    const char* digits,
    bool press_enter,
    TotpHidTypeStatusCb status_cb,
    void* status_ctx) {
    if(!totp_hid_code_is_digits(digits)) {
        return TotpHidTypeBadCode;
    }

    FuriHalUsbInterface* prev = furi_hal_usb_get_config();
    bool switched = false;
    TotpHidTypeResult result = TotpHidTypeTimeout;

    if(prev != &usb_hid) {
        totp_hid_status(status_cb, status_ctx, "Switching USB…");
        furi_hal_usb_unlock();
        /* CDC / ufbt serial will drop here — expected, not a failure. */
        if(!furi_hal_usb_set_config(&usb_hid, NULL)) {
            FURI_LOG_E(TAG, "usb_hid set_config failed");
            return TotpHidTypeUsbBusy;
        }
        switched = true;
        /* Give the USB stack time to tear down CDC and present HID. */
        furi_delay_ms(TOTP_HID_POST_SWITCH_MS);
    }

    totp_hid_status(status_cb, status_ctx, "Waiting for host…");
    if(!totp_hid_wait_connected(TOTP_HID_CONNECT_TIMEOUT_MS)) {
        FURI_LOG_W(TAG, "HID connect timeout (%u ms)", TOTP_HID_CONNECT_TIMEOUT_MS);
        result = TotpHidTypeTimeout;
        goto restore;
    }

    totp_hid_status(status_cb, status_ctx, "Typing…");
    result = TotpHidTypeOk;
    for(const char* p = digits; *p; p++) {
        if(!totp_hid_type_key(HID_ASCII_TO_KEY(*p))) {
            FURI_LOG_E(TAG, "key send failed for '%c'", *p);
            result = TotpHidTypeSendFailed;
            break;
        }
    }

    if(result == TotpHidTypeOk && press_enter) {
        if(!totp_hid_type_key(HID_KEYBOARD_RETURN)) {
            FURI_LOG_E(TAG, "Enter send failed");
            result = TotpHidTypeSendFailed;
        }
    }

    furi_hal_hid_kb_release_all();

restore:
    if(switched) {
        totp_hid_status(status_cb, status_ctx, "Restoring USB…");
        /* Clear HID callback before tearing HID down. */
        furi_hal_hid_set_state_callback(NULL, NULL);
        furi_hal_usb_unlock();
        if(!furi_hal_usb_set_config(prev, NULL)) {
            FURI_LOG_W(TAG, "restore previous USB config failed");
            /* Typing may still have succeeded; don't override Ok→error. */
        }
        /* Brief pause so host can re-enumerate CDC before caller continues. */
        furi_delay_ms(TOTP_HID_POST_SWITCH_MS);
    }
    return result;
}
