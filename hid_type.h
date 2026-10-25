#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** v1: always send Enter after the digits. */
#define TOTP_HID_ENTER_AFTER true

typedef enum {
    TotpHidTypeOk = 0,
    TotpHidTypeBadCode, /* empty / not digit-only */
    TotpHidTypeUsbBusy, /* set_config rejected */
    TotpHidTypeTimeout, /* host never brought HID up */
    TotpHidTypeSendFailed, /* connected but key reports failed */
    TotpHidTypeBtFail, /* BLE profile start / type failed */
    TotpHidTypeBtDisabled, /* Settings → Bluetooth Off */
    TotpHidTypeBtNeedPair, /* no bonds — prompt Settings → Pair */
    TotpHidTypePending, /* async USB/BT type in flight (GUI not blocked) */
    TotpHidTypeCancelled, /* user Back / cancel during wait or type */
} TotpHidTypeResult;

typedef enum {
    TotpHidTransportUsb = 0,
    TotpHidTransportBt,
} TotpHidTransport;

/** Optional status line for UI (e.g. popup). May be called several times. */
typedef void (*TotpHidTypeStatusCb)(const char* status, void* context);

/** Request cancel of in-flight USB type_digits (safe from GUI thread). */
void totp_hid_type_request_cancel(void);
void totp_hid_type_clear_cancel(void);
bool totp_hid_type_is_cancelled(void);

/**
 * Type a digit-only string over USB HID keyboard.
 * Switches USB to `usb_hid` if needed, waits for host HID ready (CDC drop
 * during the switch is expected and not a failure), types each digit,
 * optionally Enter, then restores the prior USB mode.
 * Honors totp_hid_type_request_cancel(); returns TotpHidTypeCancelled.
 * Call from a worker thread — do not block the GUI.
 */
TotpHidTypeResult totp_hid_type_digits(
    const char* digits,
    bool press_enter,
    TotpHidTypeStatusCb status_cb,
    void* status_ctx);

#ifdef __cplusplus
}
#endif
