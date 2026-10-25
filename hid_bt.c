#include "hid_bt.h"

#include <furi.h>
#include <furi_hal_bt.h>
#include <furi_hal_usb_hid.h>
#include <bt/bt_service/bt.h>
#include <extra_profiles/hid_profile.h>
#include <storage/storage.h>

#include <stdio.h>
#include <string.h>

#define TAG "TotpHidBt"

#define TOTP_BT_KEY_DELAY_MS 25
#define TOTP_BT_KEY_RETRY 6
#define TOTP_BT_POLL_MS 50

struct TotpHidBt {
    Bt* bt;
    FuriHalBleProfileBase* profile;
    volatile bool connected;
    bool enabled;
};

static void totp_hid_bt_status_cb(BtStatus status, void* context) {
    TotpHidBt* inst = context;
    inst->connected = (status == BtStatusConnected);
    FURI_LOG_I(TAG, "BT status %d connected=%d", status, inst->connected);
}

TotpHidBt* totp_hid_bt_alloc(void) {
    TotpHidBt* inst = malloc(sizeof(TotpHidBt));
    memset(inst, 0, sizeof(TotpHidBt));
    return inst;
}

void totp_hid_bt_free(TotpHidBt* inst) {
    furi_assert(inst);
    totp_hid_bt_disable(inst);
    free(inst);
}

bool totp_hid_bt_is_enabled(const TotpHidBt* inst) {
    return inst && inst->enabled;
}

bool totp_hid_bt_is_connected(const TotpHidBt* inst) {
    return inst && inst->enabled && inst->connected;
}

bool totp_hid_bt_enable(TotpHidBt* inst) {
    furi_assert(inst);
    if(inst->enabled) return true;

    inst->bt = furi_record_open(RECORD_BT);
    bt_disconnect(inst->bt);
    /* Wait for 2nd core NVM sync (required before profile switch). */
    furi_delay_ms(200);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, STORAGE_APP_DATA_PATH_PREFIX);
    furi_record_close(RECORD_STORAGE);

    bt_keys_storage_set_storage_path(inst->bt, APP_DATA_PATH(TOTP_BT_KEYS_STORAGE_NAME));

    BleProfileHidParams params = {
        .device_name_prefix = TOTP_BT_NAME_PREFIX,
        .mac_xor = TOTP_BT_MAC_XOR,
    };
    inst->profile = bt_profile_start(inst->bt, ble_profile_hid, &params);
    if(!inst->profile) {
        FURI_LOG_E(TAG, "bt_profile_start(ble_profile_hid) failed");
        bt_keys_storage_set_default_path(inst->bt);
        furi_record_close(RECORD_BT);
        inst->bt = NULL;
        return false;
    }

    furi_hal_bt_start_advertising();
    bt_set_status_changed_callback(inst->bt, totp_hid_bt_status_cb, inst);
    inst->enabled = true;
    inst->connected = false;
    FURI_LOG_I(TAG, "BLE HID enabled, advertising as %s-*", TOTP_BT_NAME_PREFIX);
    return true;
}

void totp_hid_bt_disable(TotpHidBt* inst) {
    furi_assert(inst);
    if(!inst->enabled || !inst->bt) {
        inst->enabled = false;
        return;
    }

    bt_set_status_changed_callback(inst->bt, NULL, NULL);
    bt_disconnect(inst->bt);
    furi_delay_ms(200);
    bt_keys_storage_set_default_path(inst->bt);
    furi_check(bt_profile_restore_default(inst->bt));
    furi_delay_ms(200);
    furi_hal_bt_start_advertising();

    furi_record_close(RECORD_BT);
    inst->bt = NULL;
    inst->profile = NULL;
    inst->enabled = false;
    inst->connected = false;
    FURI_LOG_I(TAG, "BLE HID disabled, default BT restored");
}

void totp_hid_bt_forget_bonds(TotpHidBt* inst) {
    furi_assert(inst);
    bool was = inst->enabled;
    if(!was) {
        if(!totp_hid_bt_enable(inst)) return;
    }
    bt_forget_bonded_devices(inst->bt);
    furi_delay_ms(200);
    if(!was) {
        totp_hid_bt_disable(inst);
    } else {
        /* Re-advertise after forget. */
        furi_hal_bt_start_advertising();
        inst->connected = false;
    }
}

static bool totp_hid_bt_type_key(TotpHidBt* inst, uint16_t key) {
    if(key == HID_KEYBOARD_NONE || !inst->profile) return false;
    for(uint8_t attempt = 0; attempt < TOTP_BT_KEY_RETRY; attempt++) {
        if(ble_profile_hid_kb_press(inst->profile, key)) {
            furi_delay_ms(TOTP_BT_KEY_DELAY_MS);
            if(ble_profile_hid_kb_release(inst->profile, key)) {
                furi_delay_ms(TOTP_BT_KEY_DELAY_MS);
                return true;
            }
            ble_profile_hid_kb_release_all(inst->profile);
            furi_delay_ms(TOTP_BT_KEY_DELAY_MS);
        } else {
            furi_delay_ms(TOTP_BT_KEY_DELAY_MS * 2);
        }
    }
    return false;
}

bool totp_hid_bt_type_digits(
    TotpHidBt* bt,
    const char* digits,
    bool press_enter,
    uint32_t timeout_ms,
    void (*status_cb)(const char* status, void* ctx),
    void* status_ctx) {
    if(!bt || !digits || !digits[0]) return false;
    for(const char* p = digits; *p; p++) {
        if(*p < '0' || *p > '9') return false;
    }

    if(!totp_hid_bt_enable(bt)) return false;

    if(status_cb) status_cb("Waiting for BT…", status_ctx);
    uint32_t waited = 0;
    while(!bt->connected && waited < timeout_ms) {
        if((waited % 1000) == 0 && status_cb) {
            char line[32];
            snprintf(line, sizeof(line), "BT wait… %lus", (unsigned long)(waited / 1000));
            status_cb(line, status_ctx);
        }
        furi_delay_ms(TOTP_BT_POLL_MS);
        waited += TOTP_BT_POLL_MS;
    }
    if(!bt->connected) {
        FURI_LOG_W(TAG, "BT HID connect timeout");
        return false;
    }

    furi_delay_ms(300);
    if(status_cb) status_cb("Typing…", status_ctx);

    bool ok = true;
    for(const char* p = digits; *p; p++) {
        if(!totp_hid_bt_type_key(bt, HID_ASCII_TO_KEY(*p))) {
            ok = false;
            break;
        }
    }
    if(ok && press_enter) {
        if(!totp_hid_bt_type_key(bt, HID_KEYBOARD_RETURN)) ok = false;
    }
    ble_profile_hid_kb_release_all(bt->profile);
    return ok;
}
