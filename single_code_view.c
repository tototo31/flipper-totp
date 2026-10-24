#include "single_code_view.h"
#include "app.h"

#include <gui/elements.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    TotpApp* app;
    char name[YKOATH_NAME_MAX];
    char code[16];
    char footer[24];
} SingleCodeViewModel;

struct SingleCodeView {
    View* view;
};

static void single_code_view_fill(SingleCodeViewModel* model) {
    TotpApp* app = model->app;
    if(!app) {
        model->name[0] = model->code[0] = model->footer[0] = '\0';
        return;
    }

    const char* name = app->name_buffer;
    if((!name || !name[0]) && app->credentials.count > 0) {
        name = app->credentials.items[0].name;
    }
    snprintf(model->name, sizeof(model->name), "%s", name ? name : "");

    if(app->credentials.count > 0 && app->credentials.items[0].code[0]) {
        snprintf(model->code, sizeof(model->code), "%s", app->credentials.items[0].code);
    } else {
        snprintf(model->code, sizeof(model->code), "******");
    }

    uint64_t now = totp_app_unix_utc(app);
    if(app->codes_live && app->code_expires_utc > now) {
        snprintf(
            model->footer,
            sizeof(model->footer),
            "%lus remaining",
            (unsigned long)(app->code_expires_utc - now));
    } else {
        snprintf(model->footer, sizeof(model->footer), "Expired — OK reread");
    }
}

static void single_code_view_draw_callback(Canvas* canvas, void* _model) {
    SingleCodeViewModel* model = _model;
    canvas_clear(canvas);

    /* Name — top, centered, truncate to fit. */
    canvas_set_font(canvas, FontSecondary);
    char name_fit[YKOATH_NAME_MAX];
    snprintf(name_fit, sizeof(name_fit), "%s", model->name);
    while(name_fit[0] && canvas_string_width(canvas, name_fit) > 124) {
        size_t len = strlen(name_fit);
        if(len <= 1) break;
        name_fit[len - 1] = '\0';
    }
    canvas_draw_str_aligned(canvas, 64, 10, AlignCenter, AlignBottom, name_fit);

    /* OTP digits — front and center. */
    canvas_set_font(canvas, FontBigNumbers);
    canvas_draw_str_aligned(canvas, 64, 40, AlignCenter, AlignCenter, model->code);

    /* Countdown / reread hint — bottom. */
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 60, AlignCenter, AlignBottom, model->footer);
}

static bool single_code_view_input_callback(InputEvent* event, void* context) {
    SingleCodeView* view = context;
    if(event->type != InputTypeShort) {
        return false;
    }
    if(event->key != InputKeyOk) {
        return false;
    }

    bool consumed = false;
    with_view_model(
        view->view,
        SingleCodeViewModel * model,
        {
            TotpApp* app = model->app;
            if(app && !app->codes_live) {
                view_dispatcher_send_custom_event(
                    app->view_dispatcher, TotpCustomEventCodesRefresh);
                consumed = true;
            }
            single_code_view_fill(model);
        },
        true);
    return consumed;
}

SingleCodeView* single_code_view_alloc(void) {
    SingleCodeView* view = malloc(sizeof(SingleCodeView));
    view->view = view_alloc();
    view_allocate_model(view->view, ViewModelTypeLocking, sizeof(SingleCodeViewModel));
    view_set_context(view->view, view);
    view_set_draw_callback(view->view, single_code_view_draw_callback);
    view_set_input_callback(view->view, single_code_view_input_callback);
    with_view_model(
        view->view,
        SingleCodeViewModel * model,
        { memset(model, 0, sizeof(SingleCodeViewModel)); },
        true);
    return view;
}

void single_code_view_free(SingleCodeView* view) {
    furi_assert(view);
    view_free(view->view);
    free(view);
}

View* single_code_view_get_view(SingleCodeView* view) {
    return view->view;
}

void single_code_view_set_app(SingleCodeView* view, struct TotpApp* app) {
    with_view_model(
        view->view,
        SingleCodeViewModel * model,
        {
            model->app = app;
            single_code_view_fill(model);
        },
        true);
}

void single_code_view_update(SingleCodeView* view) {
    with_view_model(
        view->view,
        SingleCodeViewModel * model,
        { single_code_view_fill(model); },
        true);
}
