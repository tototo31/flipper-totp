#include "settings_view.h"
#include "app.h"

#include <gui/elements.h>
#include <stdio.h>
#include <string.h>

#define SETTINGS_HDR_H 12
#define SETTINGS_ROW_H 13
#define SETTINGS_LIST_TOP SETTINGS_HDR_H
#define SETTINGS_VISIBLE ((64 - SETTINGS_LIST_TOP) / SETTINGS_ROW_H)
#define SETTINGS_VALUE_X 126

typedef struct {
    TotpApp* app;
    uint8_t selected;
    uint8_t window;
    char local_value[16];
    char utc_value[16];
    char tz_value[12];
} SettingsViewModel;

struct SettingsView {
    View* view;
};

static const char* const settings_labels[SettingsViewItemCount] = {
    "Local",
    "UTC",
    "Offset Mins",
    "Set TZ from UTC",
    "Edit TZ minutes",
    "Set OATH password",
    "Clear OATH password",
    "Reset OATH applet",
};

static bool settings_view_item_has_value(uint8_t index) {
    return index <= SettingsViewItemOffset;
}

static void settings_view_clamp(SettingsViewModel* model) {
    if(model->selected >= SettingsViewItemCount) {
        model->selected = SettingsViewItemCount - 1;
    }
    if(model->selected < model->window) {
        model->window = model->selected;
    }
    while(model->selected >= model->window + SETTINGS_VISIBLE) {
        model->window++;
    }
}

static void settings_view_fill_values(SettingsViewModel* model) {
    TotpApp* app = model->app;
    if(!app) {
        model->local_value[0] = model->utc_value[0] = model->tz_value[0] = '\0';
        return;
    }
    snprintf(
        model->local_value,
        sizeof(model->local_value),
        "%lu",
        (unsigned long)totp_app_rtc_local_unix());
    snprintf(
        model->utc_value,
        sizeof(model->utc_value),
        "%lu",
        (unsigned long)totp_app_unix_utc(app));
    snprintf(
        model->tz_value, sizeof(model->tz_value), "%d", (int)app->prefs.tz_offset_minutes);
}

static const char* settings_view_value(SettingsViewModel* model, uint8_t index) {
    if(index == SettingsViewItemLocal) return model->local_value;
    if(index == SettingsViewItemUtc) return model->utc_value;
    if(index == SettingsViewItemOffset) return model->tz_value;
    return "";
}

static void settings_view_draw_callback(Canvas* canvas, void* _model) {
    SettingsViewModel* model = _model;
    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "Settings");
    canvas_draw_line(canvas, 0, SETTINGS_HDR_H - 1, 128, SETTINGS_HDR_H - 1);

    canvas_set_font(canvas, FontSecondary);
    for(uint8_t row = 0; row < SETTINGS_VISIBLE; row++) {
        uint8_t i = model->window + row;
        if(i >= SettingsViewItemCount) break;

        int y = SETTINGS_LIST_TOP + row * SETTINGS_ROW_H;
        bool sel = (i == model->selected);
        if(sel) {
            canvas_draw_box(canvas, 0, y, 128, SETTINGS_ROW_H);
            canvas_invert_color(canvas);
        }

        const char* label = settings_labels[i];
        canvas_draw_str(canvas, 2, y + 10, label);

        if(settings_view_item_has_value(i)) {
            const char* value = settings_view_value(model, i);
            /* Keep value on one line: shrink from left with ellipsis if needed. */
            char fit[16];
            snprintf(fit, sizeof(fit), "%s", value);
            uint16_t label_w = canvas_string_width(canvas, label);
            uint16_t max_w = (SETTINGS_VALUE_X > label_w + 8) ? (SETTINGS_VALUE_X - label_w - 8) : 40;
            if(canvas_string_width(canvas, fit) > max_w) {
                /* Prefer keeping the rightmost digits (unix uniqueness). */
                while(fit[0] && canvas_string_width(canvas, fit) > max_w) {
                    memmove(fit, fit + 1, strlen(fit));
                }
            }
            canvas_draw_str_aligned(
                canvas, SETTINGS_VALUE_X, y + 10, AlignRight, AlignBottom, fit);
        }

        if(sel) {
            canvas_invert_color(canvas);
        }
    }
}

static bool settings_view_input_callback(InputEvent* event, void* context) {
    SettingsView* settings_view = context;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) {
        return false;
    }

    bool consumed = false;
    with_view_model(
        settings_view->view,
        SettingsViewModel * model,
        {
            if(event->key == InputKeyUp) {
                if(model->selected > 0) {
                    model->selected--;
                    settings_view_clamp(model);
                }
                consumed = true;
            } else if(event->key == InputKeyDown) {
                if(model->selected + 1 < SettingsViewItemCount) {
                    model->selected++;
                    settings_view_clamp(model);
                }
                consumed = true;
            } else if(event->key == InputKeyOk) {
                if(model->app) {
                    view_dispatcher_send_custom_event(
                        model->app->view_dispatcher, model->selected);
                }
                consumed = true;
            }
            settings_view_fill_values(model);
        },
        true);

    return consumed;
}

SettingsView* settings_view_alloc(void) {
    SettingsView* settings_view = malloc(sizeof(SettingsView));
    settings_view->view = view_alloc();
    view_allocate_model(settings_view->view, ViewModelTypeLocking, sizeof(SettingsViewModel));
    view_set_context(settings_view->view, settings_view);
    view_set_draw_callback(settings_view->view, settings_view_draw_callback);
    view_set_input_callback(settings_view->view, settings_view_input_callback);
    with_view_model(
        settings_view->view,
        SettingsViewModel * model,
        {
            memset(model, 0, sizeof(SettingsViewModel));
            model->selected = SettingsViewItemOffset;
        },
        true);
    return settings_view;
}

void settings_view_free(SettingsView* settings_view) {
    furi_assert(settings_view);
    view_free(settings_view->view);
    free(settings_view);
}

View* settings_view_get_view(SettingsView* settings_view) {
    return settings_view->view;
}

void settings_view_set_app(SettingsView* settings_view, struct TotpApp* app) {
    with_view_model(
        settings_view->view,
        SettingsViewModel * model,
        {
            model->app = app;
            settings_view_fill_values(model);
        },
        true);
}

void settings_view_reset_selection(SettingsView* settings_view) {
    with_view_model(
        settings_view->view,
        SettingsViewModel * model,
        {
            model->selected = SettingsViewItemOffset;
            model->window = 0;
            settings_view_clamp(model);
            settings_view_fill_values(model);
        },
        true);
}

void settings_view_update(SettingsView* settings_view) {
    with_view_model(
        settings_view->view,
        SettingsViewModel * model,
        {
            settings_view_clamp(model);
            settings_view_fill_values(model);
        },
        true);
}
