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

typedef enum {
    TotpCustomEventNfcCardDetected,
    TotpCustomEventNfcSuccess,
    TotpCustomEventNfcNeedPin,
    TotpCustomEventNfcError,
    TotpCustomEventCodesRefresh,
    TotpCustomEventCodesSelect,
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
    char name_buffer[YKOATH_NAME_MAX];
    char secret_buffer[YKOATH_SECRET_TEXT_MAX];
    uint8_t put_digits;
    char result_message[64];
    int32_t utc_input;

    FuriMutex* cli_mutex;
    bool cli_put_busy; /* CLI armed or PUT scan in flight from CLI */
} TotpApp;

/** Naive unix of Flipper RTC wall clock (local, not UTC). */
uint32_t totp_app_rtc_local_unix(void);
/** True UTC unix for YKOATH: local_naive − tz_offset_minutes×60. */
uint64_t totp_app_unix_utc(const TotpApp* app);
void totp_app_copy_credentials(TotpApp* app);
void totp_app_begin_scan(TotpApp* app, NfcSessionOp op);
void totp_app_clear_secret(TotpApp* app);
void totp_app_finish_put(TotpApp* app);
void totp_cli_register(TotpApp* app);
void totp_cli_unregister(TotpApp* app);
