#include "hid_type.h"

#include <furi.h>
#include <furi_hal_usb.h>
#include <furi_hal_usb_hid.h>

#define TOTP_HID_KEY_DELAY_MS 12
#define TOTP_HID_CONNECT_POLL_MS 20
#define TOTP_HID_CONNECT_TIMEOUT_MS 2000
#define TOTP_HID_SETTLE_MS 80

static bool totp_hid_code_is_digits(const char* digits) {
    if(!digits || !digits[0]) return false;
    for(const char* p = digits; *p; p++) {
        if(*p < '0' || *p > '9') return false;
    }
    return true;
}

static bool totp_hid_wait_connected(uint32_t timeout_ms) {
    uint32_t waited = 0;
    while(!furi_hal_hid_is_connected()) {
        if(waited >= timeout_ms) return false;
        furi_delay_ms(TOTP_HID_CONNECT_POLL_MS);
        waited += TOTP_HID_CONNECT_POLL_MS;
    }
    furi_delay_ms(TOTP_HID_SETTLE_MS);
    return true;
}

static bool totp_hid_type_key(uint16_t key) {
    if(key == HID_KEYBOARD_NONE) return false;
    if(!furi_hal_hid_kb_press(key)) return false;
    furi_delay_ms(TOTP_HID_KEY_DELAY_MS);
    if(!furi_hal_hid_kb_release(key)) return false;
    furi_delay_ms(TOTP_HID_KEY_DELAY_MS);
    return true;
}

bool totp_hid_type_digits(const char* digits, bool press_enter) {
    if(!totp_hid_code_is_digits(digits)) return false;

    FuriHalUsbInterface* prev = furi_hal_usb_get_config();
    bool switched = false;
    bool ok = false;

    if(prev != &usb_hid) {
        furi_hal_usb_unlock();
        if(!furi_hal_usb_set_config(&usb_hid, NULL)) {
            return false;
        }
        switched = true;
    }

    if(!totp_hid_wait_connected(TOTP_HID_CONNECT_TIMEOUT_MS)) {
        goto restore;
    }

    ok = true;
    for(const char* p = digits; *p; p++) {
        if(!totp_hid_type_key(HID_ASCII_TO_KEY(*p))) {
            ok = false;
            break;
        }
    }

    if(ok && press_enter) {
        if(!totp_hid_type_key(HID_KEYBOARD_RETURN)) {
            ok = false;
        }
    }

    furi_hal_hid_kb_release_all();

restore:
    if(switched) {
        furi_hal_usb_set_config(prev, NULL);
    }
    return ok;
}
