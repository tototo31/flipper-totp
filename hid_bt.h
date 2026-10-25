#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TOTP_BT_KEYS_STORAGE_NAME ".totp_nfc_bt.keys"
#define TOTP_BT_NAME_PREFIX "HID"
#define TOTP_BT_MAC_XOR 0x70A7

typedef struct TotpHidBt TotpHidBt;

TotpHidBt* totp_hid_bt_alloc(void);
void totp_hid_bt_free(TotpHidBt* bt);

/** Start BLE HID profile + advertising (idempotent). */
bool totp_hid_bt_enable(TotpHidBt* bt);

/** Restore default Flipper BT profile (call on app exit / leave BT mode). */
void totp_hid_bt_disable(TotpHidBt* bt);

bool totp_hid_bt_is_enabled(const TotpHidBt* bt);
bool totp_hid_bt_is_connected(const TotpHidBt* bt);

/** Forget bonded HID hosts for this app's key store. */
void totp_hid_bt_forget_bonds(TotpHidBt* bt);

/**
 * Type digit string over BLE HID. Requires enable() first.
 * Waits up to timeout_ms for a host connection.
 */
bool totp_hid_bt_type_digits(
    TotpHidBt* bt,
    const char* digits,
    bool press_enter,
    uint32_t timeout_ms,
    void (*status_cb)(const char* status, void* ctx),
    void* status_ctx);

#ifdef __cplusplus
}
#endif
