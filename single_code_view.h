#pragma once

#include <gui/view.h>

struct TotpApp;
typedef struct SingleCodeView SingleCodeView;

SingleCodeView* single_code_view_alloc(void);
void single_code_view_free(SingleCodeView* view);
View* single_code_view_get_view(SingleCodeView* view);

void single_code_view_set_app(SingleCodeView* view, struct TotpApp* app);
void single_code_view_update(SingleCodeView* view);
