#pragma once

#include <gui/view.h>

struct TotpApp;
typedef struct SettingsView SettingsView;

typedef enum {
    SettingsViewItemLocal = 0,
    SettingsViewItemUtc,
    SettingsViewItemOffset,
    SettingsViewItemFromUtc,
    SettingsViewItemEditTz,
    SettingsViewItemAutoUnlock,
    SettingsViewItemSetPassword,
    SettingsViewItemClearPassword,
    SettingsViewItemReset,
    SettingsViewItemCount,
} SettingsViewItem;

SettingsView* settings_view_alloc(void);
void settings_view_free(SettingsView* settings_view);
View* settings_view_get_view(SettingsView* settings_view);

void settings_view_set_app(SettingsView* settings_view, struct TotpApp* app);
void settings_view_reset_selection(SettingsView* settings_view);
void settings_view_update(SettingsView* settings_view);
