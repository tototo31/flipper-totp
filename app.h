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
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <notification/notification_messages.h>

#define TAG "TotpNfc"

typedef enum {
    TotpViewSubmenu,
    TotpViewPopup,
    TotpViewTextInput,
    TotpViewNumberInput,
    TotpViewVarList,
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
} TotpCustomEvent;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    NotificationApp* notifications;

    Submenu* submenu;
    Popup* popup;
    TextInput* text_input;
    NumberInput* number_input;
    VariableItemList* var_list;
    Widget* widget;

    NfcSession* nfc_session;
    TotpPrefs prefs;

    NfcSessionOp pending_op;
    YkoathCredentialList credentials;
    size_t selected_cred;
    uint64_t code_expires_utc; /* unix time when current codes become invalid */
    bool codes_live; /* false → stars; OK starts a reread */
    char codes_header[40]; /* persistent — VariableItemList keeps pointers */

    char pin_buffer[TOTP_PREFS_PASSWORD_MAX];
    char name_buffer[YKOATH_NAME_MAX];
    char secret_buffer[YKOATH_SECRET_TEXT_MAX];
    uint8_t put_digits;
    char tz_label[16];
    char result_message[64];
} TotpApp;

uint64_t totp_app_unix_utc(const TotpApp* app);
void totp_app_copy_credentials(TotpApp* app);
void totp_app_begin_scan(TotpApp* app, NfcSessionOp op);
