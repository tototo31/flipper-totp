#include "codes_view.h"
#include "app.h"

#include <gui/elements.h>
#include <furi/core/string.h>
#include <stdio.h>
#include <string.h>

#define CODES_HDR_H 12
#define CODES_ROW_H 12
#define CODES_LINE_H 10
#define CODES_LIST_TOP CODES_HDR_H
#define CODES_LIST_H (64 - CODES_LIST_TOP)
#define CODES_NAME_MAX_LINES 3
#define CODES_GAP 4

typedef struct {
    TotpApp* app;
    uint8_t selected;
    uint8_t window;
    char header[24];
} CodesViewModel;

struct CodesView {
    View* view;
};

/** Approx FontSecondary advance for clamp estimates (no canvas). */
#define CODES_CHAR_W_EST 6

static uint16_t codes_view_name_max_w(uint16_t code_w) {
    /* Leave a gap between name and right-aligned digits. */
    if(code_w + CODES_GAP >= 124) return 36;
    return (uint16_t)(124 - code_w - CODES_GAP);
}

static int codes_view_estimate_row_h(const char* name, const char* code, bool sel) {
    if(!sel) return CODES_ROW_H;
    size_t code_len = code ? strlen(code) : 0;
    uint16_t code_w = (uint16_t)(code_len * CODES_CHAR_W_EST);
    uint16_t max_w = codes_view_name_max_w(code_w);
    size_t chars_per = max_w / CODES_CHAR_W_EST;
    if(chars_per < 4) chars_per = 4;
    size_t len = name ? strlen(name) : 0;
    size_t lines = len ? ((len + chars_per - 1) / chars_per) : 1;
    if(lines > CODES_NAME_MAX_LINES) lines = CODES_NAME_MAX_LINES;
    if(lines < 1) lines = 1;
    int h = (int)lines * CODES_LINE_H + 2;
    return (h < CODES_ROW_H) ? CODES_ROW_H : h;
}

/**
 * Greedy character wrap into up to max_lines buffers.
 * Returns number of lines written.
 */
static size_t codes_view_wrap_name(
    Canvas* canvas,
    const char* name,
    uint16_t max_w,
    char lines[][YKOATH_NAME_MAX],
    size_t max_lines) {
    if(!name) name = "";
    size_t nlines = 0;
    size_t pos = 0;
    size_t namelen = strlen(name);

    while(nlines < max_lines && pos < namelen) {
        size_t end = pos;
        size_t last_fit = pos;
        while(end < namelen) {
            end++;
            char tmp[YKOATH_NAME_MAX];
            size_t take = end - pos;
            if(take >= sizeof(tmp)) take = sizeof(tmp) - 1;
            memcpy(tmp, name + pos, take);
            tmp[take] = '\0';
            if(canvas_string_width(canvas, tmp) > max_w) {
                end--;
                break;
            }
            last_fit = end;
        }
        if(last_fit == pos) {
            /* Single glyph wider than max_w — force one char. */
            last_fit = pos + 1;
        }
        /* Last line: pack remainder (may clip visually if still too long). */
        if(nlines + 1 == max_lines && last_fit < namelen) {
            last_fit = namelen;
        }
        size_t take = last_fit - pos;
        if(take >= YKOATH_NAME_MAX) take = YKOATH_NAME_MAX - 1;
        memcpy(lines[nlines], name + pos, take);
        lines[nlines][take] = '\0';
        /* If last line still too wide, trim. */
        while(lines[nlines][0] && canvas_string_width(canvas, lines[nlines]) > max_w) {
            size_t L = strlen(lines[nlines]);
            if(L <= 1) break;
            lines[nlines][L - 1] = '\0';
        }
        nlines++;
        pos = last_fit;
    }
    if(nlines == 0) {
        lines[0][0] = '\0';
        nlines = 1;
    }
    return nlines;
}

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

    /* Scroll window forward until selected row fits in the list area. */
    while(model->window < model->selected) {
        int used = 0;
        bool shown = false;
        for(size_t i = model->window; i < n; i++) {
            YkoathCredential* c = &app->credentials.items[i];
            int h = codes_view_estimate_row_h(c->name, c->code, i == model->selected);
            if(used > 0 && used + h > CODES_LIST_H) break;
            used += h;
            if(i == model->selected) {
                shown = true;
                break;
            }
        }
        if(shown) break;
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
    int y = CODES_LIST_TOP;

    for(size_t i = model->window; i < n && y < 64; i++) {
        YkoathCredential* cred = &app->credentials.items[i];
        bool sel = (i == model->selected);
        uint16_t code_w = canvas_string_width(canvas, cred->code);
        uint16_t max_w = codes_view_name_max_w(code_w);

        int row_h = CODES_ROW_H;
        char lines[CODES_NAME_MAX_LINES][YKOATH_NAME_MAX];
        size_t nlines = 1;
        lines[0][0] = '\0';

        if(sel) {
            nlines = codes_view_wrap_name(
                canvas, cred->name, max_w, lines, CODES_NAME_MAX_LINES);
            row_h = (int)nlines * CODES_LINE_H + 2;
            if(row_h < CODES_ROW_H) row_h = CODES_ROW_H;
        }

        if(y + row_h > 64 && i != model->selected && y > CODES_LIST_TOP) {
            break;
        }

        if(sel) {
            canvas_draw_box(canvas, 0, y, 128, row_h);
            canvas_invert_color(canvas);
            for(size_t L = 0; L < nlines; L++) {
                canvas_draw_str(
                    canvas, 2, y + (int)L * CODES_LINE_H + CODES_LINE_H, lines[L]);
            }
            /* Digits: single line, top-right of the highlight block — never wrap. */
            canvas_draw_str_aligned(
                canvas, 126, y + CODES_LINE_H, AlignRight, AlignBottom, cred->code);
            canvas_invert_color(canvas);
        } else {
            FuriString* name_fit = furi_string_alloc_set_str(cred->name);
            elements_string_fit_width(canvas, name_fit, max_w);
            canvas_draw_str(canvas, 2, y + 9, furi_string_get_cstr(name_fit));
            furi_string_free(name_fit);
            canvas_draw_str_aligned(canvas, 126, y + 9, AlignRight, AlignBottom, cred->code);
        }

        y += row_h;
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

void codes_view_set_selection(CodesView* codes_view, uint8_t index) {
    with_view_model(
        codes_view->view,
        CodesViewModel * model,
        {
            model->selected = index;
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
