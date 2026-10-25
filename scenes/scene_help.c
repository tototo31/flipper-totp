#include "../app.h"
#include "scenes.h"

/* Concise copy for the 128x64 scrollable widget. */
static const char* const totp_help_text =
    "\e#NFC TOTP\n"
    "Reads and manages OATH-TOTP "
    "over NFC via YKOATH (same "
    "protocol as Yubico "
    "Authenticator).\n"
    "\n"
    "\e#What it does\n"
    "Read TOTP — CALCULATE ALL\n"
    "List — LIST accounts\n"
    "  then Calculate or Delete\n"
    "Add — PUT name + secret\n"
    "Receive secret — USB CLI\n"
    "  totp_put (RAM only)\n"
    "Settings — TZ / clock, "
    "SET CODE / CLEAR / RESET.\n"
    "OATH password: enter on "
    "device when challenged; "
    "kept in RAM unless "
    "Auto-unlock is enabled "
    "(warns: stored on Flipper).\n"
    "\n"
    "\e#Compatible devices\n"
    "VivoKey Apex "
    "(apex-totp / YKOATH)\n"
    "YubiKey OATH applet "
    "(NFC)\n"
    "Other ISO14443-4A cards "
    "with YubiKey OATH AID:\n"
    "A0 00 00 05 27 21 01\n"
    "\n"
    "\e#Clock / TZ\n"
    "Flipper RTC is local. In "
    "Settings, enter phone UTC "
    "unix to auto-set TZ "
    "(local−UTC min), or edit "
    "TZ manually. US Central: "
    "CDT -300, CST -360.\n"
    "\n"
    "\e#Codes list / Type\n"
    "Read TOTP list: short OK = "
    "open/recalc that account; "
    "hold OK = recalc all. "
    "Single-code: Right = Type "
    "(USB or BT per Settings), "
    "Left = the other when BT On. "
    "Settings: Bluetooth On/Off "
    "(Off = radio quiet), Type "
    "over, Pair (HID-<name>), "
    "Forget pairings. No pairing "
    "→ Type BT opens Settings. "
    "USB HID may drop CDC/ufbt.\n"
    "\n"
    "made by tototo31";

void totp_scene_help_on_enter(void* context) {
    TotpApp* app = context;
    widget_reset(app->widget);
    widget_add_text_scroll_element(app->widget, 0, 0, 128, 64, totp_help_text);
    view_dispatcher_switch_to_view(app->view_dispatcher, TotpViewWidget);
}

bool totp_scene_help_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void totp_scene_help_on_exit(void* context) {
    TotpApp* app = context;
    widget_reset(app->widget);
}
