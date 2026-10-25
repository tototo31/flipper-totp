#include "hid_bt.h"

#include <furi.h>
#include <furi_hal_bt.h>
#include <furi_hal_usb_hid.h>
#include <bt/bt_service/bt.h>
#include <extra_profiles/hid_profile.h>
#include <gap.h>
#include <storage/storage.h>
#include <toolbox/saved_struct.h>

#include <stdio.h>
#include <string.h>

#define TAG "TotpHidBt"

/*
 * BLE HID timing (phones): hold so key-down notifies before key-up; gap so
 * key-up notifies before the next press. Wedge uses ~2 ms (PC-oriented).
 *
 * CRITICAL: ble_profile_hid_kb_press/release return *false on success*.
 * Underlying ble_gatt_characteristic_update returns (result != SUCCESS).
 * That is the opposite of furi_hal_hid_kb_* and of the hid_profile.h docs.
 * Never treat a true return as "sent OK" — match flipper-wedge and ignore
 * the bool (or invert it). Branching on true caused: one digit typed, then
 * "HID send failed" after retries re-pressed into more report slots.
 */
#define TOTP_BT_HOLD_MS 80
#define TOTP_BT_GAP_MS 80
#define TOTP_BT_POLL_MS 50
#define TOTP_BT_POST_CONNECT_MS 500
#define TOTP_BT_POST_TYPE_MS 150

struct TotpHidBt {
    Bt* bt;
    FuriHalBleProfileBase* profile;
    volatile bool connected;
    volatile bool cancel;
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
    totp_hid_bt_restore_system(inst);
    free(inst);
}

bool totp_hid_bt_is_enabled(const TotpHidBt* inst) {
    return inst && inst->enabled;
}

bool totp_hid_bt_is_connected(const TotpHidBt* inst) {
    return inst && inst->enabled && inst->connected;
}

bool totp_hid_bt_has_bonds(void) {
    /*
     * Keys file always holds GapRootSecurityKeys even with zero pairings
     * (forget regenerates IRK/ERK and saves current_size=0). Old check
     * (file size > 16) was always true once the store existed → Type via BT
     * entered "BT wait" with no host. Real bonds add pairing_data after
     * the root keys in the saved_struct payload.
     */
    size_t payload = 0;
    if(!saved_struct_get_metadata(
           APP_DATA_PATH(TOTP_BT_KEYS_STORAGE_NAME), NULL, NULL, &payload)) {
        return false;
    }
    return payload > sizeof(GapRootSecurityKeys);
}

void totp_hid_bt_request_cancel(TotpHidBt* bt) {
    if(bt) bt->cancel = true;
}

void totp_hid_bt_clear_cancel(TotpHidBt* bt) {
    if(bt) bt->cancel = false;
}

bool totp_hid_bt_is_cancelled(const TotpHidBt* bt) {
    return bt && bt->cancel;
}

/** Sleep in short chunks so cancel is noticed during hold/gap. */
static bool totp_hid_bt_delay_cancelable(TotpHidBt* inst, uint32_t ms) {
    while(ms > 0) {
        if(inst->cancel) return false;
        uint32_t chunk = (ms > 25) ? 25 : ms;
        furi_delay_ms(chunk);
        ms -= chunk;
    }
    return !inst->cancel;
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

void totp_hid_bt_disable(TotpHidBt* inst, bool resume_advertising) {
    furi_assert(inst);
    if(inst->enabled && inst->bt) {
        bt_set_status_changed_callback(inst->bt, NULL, NULL);
        bt_disconnect(inst->bt);
        furi_delay_ms(200);
        bt_keys_storage_set_default_path(inst->bt);
        furi_check(bt_profile_restore_default(inst->bt));
        furi_delay_ms(200);
        furi_record_close(RECORD_BT);
        inst->bt = NULL;
        inst->profile = NULL;
    }
    inst->enabled = false;
    inst->connected = false;

    if(resume_advertising) {
        furi_hal_bt_start_advertising();
        FURI_LOG_I(TAG, "BLE HID off, default BT advertising");
    } else {
        furi_hal_bt_stop_advertising();
        FURI_LOG_I(TAG, "BLE HID off, advertising stopped (quiet)");
    }
}

void totp_hid_bt_radio_quiet(TotpHidBt* inst) {
    furi_assert(inst);
    totp_hid_bt_disable(inst, false);
}

void totp_hid_bt_restore_system(TotpHidBt* inst) {
    furi_assert(inst);
    totp_hid_bt_disable(inst, true);
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
        totp_hid_bt_disable(inst, true);
    } else {
        furi_hal_bt_start_advertising();
        inst->connected = false;
    }
}

/**
 * One keystroke over BLE HID (wedge-style): press → hold → release → gap.
 * Return values of ble_profile_hid_kb_* are intentionally ignored — see
 * TOTP_BT_* comment above (false == success).
 */
static bool totp_hid_bt_type_key(TotpHidBt* inst, uint16_t key) {
    if(key == HID_KEYBOARD_NONE || !inst->profile || inst->cancel) return false;

    ble_profile_hid_kb_press(inst->profile, key);
    if(!totp_hid_bt_delay_cancelable(inst, TOTP_BT_HOLD_MS)) {
        ble_profile_hid_kb_release_all(inst->profile);
        return false;
    }
    ble_profile_hid_kb_release(inst->profile, key);
    if(!totp_hid_bt_delay_cancelable(inst, TOTP_BT_GAP_MS)) {
        ble_profile_hid_kb_release_all(inst->profile);
        return false;
    }
    return true;
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

    /* Defense in depth: never advertise/wait when the key store has no bonds. */
    if(!totp_hid_bt_has_bonds()) {
        FURI_LOG_W(TAG, "BT type refused: no bonds");
        return false;
    }

    if(bt->cancel) return false;
    if(!totp_hid_bt_enable(bt)) return false;
    if(bt->cancel) return false;

    if(status_cb) status_cb("Waiting for BT…", status_ctx);
    uint32_t waited = 0;
    while(!bt->connected && waited < timeout_ms) {
        if(bt->cancel) {
            FURI_LOG_I(TAG, "BT type cancelled while waiting");
            return false;
        }
        if((waited % 1000) == 0 && status_cb) {
            char line[32];
            snprintf(line, sizeof(line), "BT wait… %lus", (unsigned long)(waited / 1000));
            status_cb(line, status_ctx);
        }
        furi_delay_ms(TOTP_BT_POLL_MS);
        waited += TOTP_BT_POLL_MS;
    }
    if(bt->cancel) return false;
    if(!bt->connected) {
        FURI_LOG_W(TAG, "BT HID connect timeout");
        return false;
    }

    /* Let the phone finish HID service discovery / param update. */
    if(!totp_hid_bt_delay_cancelable(bt, TOTP_BT_POST_CONNECT_MS)) return false;
    if(status_cb) status_cb("Typing…", status_ctx);

    for(const char* p = digits; *p; p++) {
        if(bt->cancel || !bt->connected) {
            FURI_LOG_W(TAG, "BT type abort mid-digits");
            ble_profile_hid_kb_release_all(bt->profile);
            return false;
        }
        if(!totp_hid_bt_type_key(bt, HID_ASCII_TO_KEY(*p))) {
            ble_profile_hid_kb_release_all(bt->profile);
            return false;
        }
    }
    if(press_enter && !bt->cancel) {
        if(!totp_hid_bt_delay_cancelable(bt, TOTP_BT_GAP_MS)) {
            ble_profile_hid_kb_release_all(bt->profile);
            return false;
        }
        if(!totp_hid_bt_type_key(bt, HID_KEYBOARD_RETURN)) {
            ble_profile_hid_kb_release_all(bt->profile);
            return false;
        }
    }
    ble_profile_hid_kb_release_all(bt->profile);
    if(!bt->cancel) {
        totp_hid_bt_delay_cancelable(bt, TOTP_BT_POST_TYPE_MS);
    }
    return !bt->cancel;
}
