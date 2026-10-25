#pragma once

#include <gui/view.h>

struct TotpApp;
typedef struct CodesView CodesView;

CodesView* codes_view_alloc(void);
void codes_view_free(CodesView* codes_view);
View* codes_view_get_view(CodesView* codes_view);

/** Bind app pointer (call once after alloc). */
void codes_view_set_app(CodesView* codes_view, struct TotpApp* app);

/** Reset selection when entering the scene. */
void codes_view_reset_selection(CodesView* codes_view);

/** Jump selection to an index (clamped). */
void codes_view_set_selection(CodesView* codes_view, uint8_t index);

/** Refresh pinned time strip + list (preserves selection). */
void codes_view_update(CodesView* codes_view);
