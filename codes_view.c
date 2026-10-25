#include "codes_view.h"
#include "app.h"

#include <gui/elements.h>
#include <stdio.h>
#include <string.h>

#define CODES_HDR_H 12
#define CODES_ROW_H 12
#define CODES_LIST_TOP CODES_HDR_H
#define CODES_VISIBLE ((64 - CODES_LIST_TOP) / CODES_ROW_H)

typedef struct {
    TotpApp* app;
    uint8_t selected;
    uint8_t window;
    char header[24];
} CodesViewModel;

struct CodesView {
    View* view;
};

static void codes_view_clamp_selection(CodesViewModel* model) {
    TotpApp* app = model->app;
    size_t n = app ? app->credentials.count : 0;
    if(n == 0) {
        model->selected = 0;
        model->window = 0;
        return;
    }
    if(model->selected >= n) {
        model->selected = (uint8_t)(n - 1);
    }
    if(model->selected < model->window) {
        model->window = model->selected;
    }
    while(model->selected >= model->window + CODES_VISIBLE) {
        model->window++;
    }
}

static void codes_view_fill_header(CodesViewModel* model) {
    TotpApp* app = model->app;
    if(!app) {
        model->header[0] = '\0';
        return;
    }
    uint64_t now = totp_app_unix_utc(app);
    if(app->codes_live && app->code_expires_utc > now) {
        snprintf(
            model->header,
            sizeof(model->header),
            "Codes %lus",
            (unsigned long)(app->code_expires_utc - now));
    } else {
        snprintf(model->header, sizeof(model->header), "Expired — OK / hold");
    }
}

static void codes_view_draw_callback(Canvas* canvas, void* _model) {
    CodesViewModel* model = _model;
    TotpApp* app = model->app;
    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, model->header);
    canvas_draw_line(canvas, 0, CODES_HDR_H - 1, 128, CODES_HDR_H - 1);

    canvas_set_font(canvas, FontSecondary);
    if(!app || app->credentials.count == 0) {
        canvas_draw_str(canvas, 2, CODES_LIST_TOP + 10, "No TOTP credentials");
        return;
    }

    size_t n = app->credentials.count;
    for(uint8_t row = 0; row < CODES_VISIBLE; row++) {
        size_t i = (size_t)model->window + row;
        if(i >= n) break;
        YkoathCredential* cred = &app->credentials.items[i];
        int y = CODES_LIST_TOP + row * CODES_ROW_H;
        bool sel = (i == model->selected);
        if(sel) {
            canvas_draw_box(canvas, 0, y, 128, CODES_ROW_H);
            canvas_invert_color(canvas);
        }
        char name_buf[20];
        snprintf(name_buf, sizeof(name_buf), "%s", cred->name);
        uint16_t code_w = canvas_string_width(canvas, cred->code);
        uint16_t max_w = (code_w < 120) ? (uint16_t)(120 - code_w) : 40;
        while(name_buf[0] && canvas_string_width(canvas, name_buf) > max_w) {
            size_t len = strlen(name_buf);
            if(len <= 1) break;
            name_buf[len - 1] = '\0';
        }
        canvas_draw_str(canvas, 2, y + 9, name_buf);
        canvas_draw_str_aligned(canvas, 126, y + 9, AlignRight, AlignBottom, cred->code);
        if(sel) {
            canvas_invert_color(canvas);
        }
    }
}

static bool codes_view_input_callback(InputEvent* event, void* context) {
    CodesView* codes_view = context;
    /* Short/Repeat: navigate. Short OK: single CALCULATE. Long OK: CALCULATE ALL. */
    if(event->type != InputTypeShort && event->type != InputTypeRepeat &&
       event->type != InputTypeLong) {
        return false;
    }

    bool consumed = false;
    with_view_model(
        codes_view->view,
        CodesViewModel * model,
        {
            TotpApp* app = model->app;
            if(app) {
                size_t n = app->credentials.count;
                if(event->type == InputTypeLong && event->key == InputKeyOk) {
                    /* Long-press OK → regenerate entire list. */
                    view_dispatcher_send_custom_event(
                        app->view_dispatcher, TotpCustomEventCodesRefresh);
                    consumed = true;
                } else if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
                    if(event->key == InputKeyUp) {
                        if(n > 0 && model->selected > 0) {
                            model->selected--;
                            codes_view_clamp_selection(model);
                        }
                        consumed = true;
                    } else if(event->key == InputKeyDown) {
                        if(n > 0 && (size_t)model->selected + 1 < n) {
                            model->selected++;
                            codes_view_clamp_selection(model);
                        }
                        consumed = true;
                    } else if(event->key == InputKeyOk && event->type == InputTypeShort) {
                        if(n > 0) {
                            /* Short OK → single CALCULATE for this row (live or expired). */
                            app->selected_cred = model->selected;
                            strncpy(
                                app->name_buffer,
                                app->credentials.items[model->selected].name,
                                sizeof(app->name_buffer) - 1);
                            app->name_buffer[sizeof(app->name_buffer) - 1] = '\0';
                            view_dispatcher_send_custom_event(
                                app->view_dispatcher, TotpCustomEventCodesSelect);
                        } else {
                            view_dispatcher_send_custom_event(
                                app->view_dispatcher, TotpCustomEventCodesRefresh);
                        }
                        consumed = true;
                    }
                }
                codes_view_fill_header(model);
            }
        },
        true);

    return consumed;
}

CodesView* codes_view_alloc(void) {
    CodesView* codes_view = malloc(sizeof(CodesView));
    codes_view->view = view_alloc();
    view_allocate_model(codes_view->view, ViewModelTypeLocking, sizeof(CodesViewModel));
    view_set_context(codes_view->view, codes_view);
    view_set_draw_callback(codes_view->view, codes_view_draw_callback);
    view_set_input_callback(codes_view->view, codes_view_input_callback);
    with_view_model(
        codes_view->view, CodesViewModel * model, { memset(model, 0, sizeof(CodesViewModel)); }, true);
    return codes_view;
}

void codes_view_free(CodesView* codes_view) {
    furi_assert(codes_view);
    view_free(codes_view->view);
    free(codes_view);
}

View* codes_view_get_view(CodesView* codes_view) {
    return codes_view->view;
}

void codes_view_set_app(CodesView* codes_view, struct TotpApp* app) {
    with_view_model(
        codes_view->view,
        CodesViewModel * model,
        {
            model->app = app;
            codes_view_fill_header(model);
        },
        true);
}

void codes_view_reset_selection(CodesView* codes_view) {
    with_view_model(
        codes_view->view,
        CodesViewModel * model,
        {
            model->selected = 0;
            model->window = 0;
            codes_view_clamp_selection(model);
            codes_view_fill_header(model);
        },
        true);
}

void codes_view_update(CodesView* codes_view) {
    with_view_model(
        codes_view->view,
        CodesViewModel * model,
        {
            codes_view_clamp_selection(model);
            codes_view_fill_header(model);
        },
        true);
}
