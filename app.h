#pragma once

#include "prefs.h"
#include "nfc_session.h"
#include "ykoath.h"

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/popup.h>
#include <gui/modules/text_input.h>
#include <gui/modules/number_input.h>
#include <gui/modules/widget.h>
#include <notification/notification_messages.h>

#include "codes_view.h"
#include "hid_bt.h"
#include "hid_type.h"
#include "settings_view.h"
#include "single_code_view.h"

#define TAG "TotpNfc"

typedef enum {
    TotpViewSubmenu,
    TotpViewPopup,
    TotpViewTextInput,
    TotpViewNumberInput,
    TotpViewCodes,
    TotpViewSingleCode,
    TotpViewSettings,
    TotpViewWidget,
    TotpViewCount,
} TotpViewId;

/* High base so these never collide with submenu / settings row indices (0..N).
 * Settings "Reset" was index 8 == old CliPut → false "Cannot parse secret". */
typedef enum {
    TotpCustomEventNfcCardDetected = 100,
    TotpCustomEventNfcSuccess,
    TotpCustomEventNfcNeedPin,
    TotpCustomEventNfcError,
    TotpCustomEventCodesRefresh,
    TotpCustomEventCodesSelect,
    TotpCustomEventHidType, /* Type using prefs transport (USB or BT) */
    TotpCustomEventHidTypeUsb,
    TotpCustomEventHidTypeBt,
    TotpCustomEventHidTypeStatus, /* worker → GUI: refresh BT type popup text */
    TotpCustomEventHidTypeDone, /* worker → GUI: BT type finished / cancelled */
    TotpCustomEventTextDone,
    TotpCustomEventNumberDone,
    TotpCustomEventCliPut,
} TotpCustomEvent;

typedef struct TotpApp {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    NotificationApp* notifications;

    Submenu* submenu;
    Popup* popup;
    TextInput* text_input;
    NumberInput* number_input;
    CodesView* codes_view;
    SingleCodeView* single_code_view;
    SettingsView* settings_view;
    Widget* widget;

    NfcSession* nfc_session;
    TotpPrefs prefs;

    NfcSessionOp pending_op;
    YkoathCredentialList credentials;
    size_t selected_cred;
    uint64_t code_challenge_utc; /* unix UTC used for the 8-byte TOTP challenge */
    uint64_t code_expires_utc; /* unix time when current codes become invalid */
    bool codes_live; /* false → stars; OK starts a reread */
    bool codes_single; /* true → Codes shows one CALCULATE; reread uses that name */

    char pin_buffer[TOTP_PREFS_PASSWORD_MAX];
    char set_code_password[TOTP_PREFS_PASSWORD_MAX]; /* SET CODE; kept off pin_buffer */
    char session_password[TOTP_PREFS_PASSWORD_MAX]; /* RAM-only OATH password this run */
    char name_buffer[YKOATH_NAME_MAX];
    char secret_buffer[YKOATH_SECRET_TEXT_MAX];
    uint8_t put_digits;
    char result_message[64];
    int32_t utc_input;

    FuriMutex* cli_mutex;
    bool cli_put_busy; /* CLI armed or PUT scan in flight from CLI */

    TotpHidBt* hid_bt; /* BLE HID session (enabled from Settings / Type BT) */

    /* Async BT Type (worker) so GUI stays responsive; Back cancels. */
    FuriThread* hid_type_thread;
    volatile bool hid_type_busy;
    TotpHidTypeResult hid_type_result;
    uint32_t hid_type_return_view;
    char hid_type_digits[16];
    char hid_type_status[40];
} TotpApp;

/** Naive unix of Flipper RTC wall clock (local, not UTC). */
uint32_t totp_app_rtc_local_unix(void);
/** True UTC unix for YKOATH: local_naive − tz_offset_minutes×60. */
uint64_t totp_app_unix_utc(const TotpApp* app);
/** Password for VALIDATE: prefs if Auto-unlock, else session RAM (may be NULL). */
const char* totp_app_oath_password(const TotpApp* app);
/** Store password in RAM; also prefs only when Auto-unlock is on. */
void totp_app_store_oath_password(TotpApp* app, const char* password);
/** Clear RAM session password and prefs password. */
void totp_app_clear_oath_password(TotpApp* app);
void totp_app_copy_credentials(TotpApp* app);
/**
 * Apply a single CALCULATE result: if a multi-cred Read All list is already
 * loaded, update the matching row in place (keep full list). Otherwise replace.
 */
void totp_app_apply_calculate_result(TotpApp* app);
void totp_app_begin_scan(TotpApp* app, NfcSessionOp op);
void totp_app_clear_secret(TotpApp* app);
void totp_app_finish_put(TotpApp* app);
/** Clamped index of the credential shown / acted on (Read All pick or single). */
size_t totp_app_cred_index(const TotpApp* app);
/** Live digit OTP for cred_index, or NULL if expired/starred/missing. */
const char* totp_app_live_code(const TotpApp* app);
/**
 * Type live OTP over USB or BT HID (popup status).
 * USB runs synchronously. BT with bonds runs on a worker (returns
 * TotpHidTypePending); Back cancels. Immediate BT errors (off / no pair)
 * still run on the GUI thread. Caller restores view unless Pending.
 * @param return_view TotpViewId to show after async BT completes.
 */
TotpHidTypeResult totp_app_type_live_hid(
    TotpApp* app,
    TotpHidTransport transport,
    uint32_t return_view);
/** Cancel in-flight BT type (Back). Safe if idle. */
void totp_app_hid_type_cancel(TotpApp* app);
void totp_cli_register(TotpApp* app);
void totp_cli_unregister(TotpApp* app);
