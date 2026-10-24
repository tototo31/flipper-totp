#include "app.h"

#include <cli/cli.h>
#include <toolbox/args.h>
#include <toolbox/cli/cli_command.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOTP_CLI_PUT_CMD "totp_put"

static void totp_cli_put_print_usage(void) {
    printf("Usage: %s <name> <secret> [digits]\n", TOTP_CLI_PUT_CMD);
    printf("  name    credential name (no spaces; quote if needed)\n");
    printf("  secret  Base32 or hex (RAM only; never written to SD)\n");
    printf("  digits  6 or 8 (default 6)\n");
    printf("App must be open. Arms NFC PUT — present card when prompted.\n");
}

static void totp_cli_put_callback(PipeSide* pipe, FuriString* args, void* context) {
    UNUSED(pipe);
    TotpApp* app = context;
    furi_assert(app);

    FuriString* name = furi_string_alloc();
    FuriString* secret = furi_string_alloc();
    int digits = 6;

    if(!args_read_probably_quoted_string_and_trim(args, name) ||
       !args_read_probably_quoted_string_and_trim(args, secret)) {
        totp_cli_put_print_usage();
        furi_string_free(name);
        furi_string_free(secret);
        return;
    }

    if(!furi_string_empty(args)) {
        if(!args_read_int_and_trim(args, &digits) || (digits != 6 && digits != 8)) {
            printf("Error: digits must be 6 or 8\n");
            furi_string_free(name);
            furi_string_free(secret);
            return;
        }
    }

    if(furi_string_size(name) == 0 || furi_string_size(name) >= YKOATH_NAME_MAX) {
        printf("Error: name empty or too long (max %d)\n", YKOATH_NAME_MAX - 1);
        furi_string_free(name);
        furi_string_free(secret);
        return;
    }
    if(furi_string_size(secret) == 0 || furi_string_size(secret) >= YKOATH_SECRET_TEXT_MAX) {
        printf("Error: secret empty or too long (max %d)\n", YKOATH_SECRET_TEXT_MAX - 1);
        furi_string_free(name);
        furi_string_free(secret);
        return;
    }

    furi_check(furi_mutex_acquire(app->cli_mutex, FuriWaitForever) == FuriStatusOk);
    if(app->cli_put_busy || nfc_session_is_running(app->nfc_session)) {
        furi_mutex_release(app->cli_mutex);
        printf("Error: NFC busy — finish or cancel current op\n");
        furi_string_free(name);
        furi_string_free(secret);
        return;
    }

    totp_app_clear_secret(app);
    strncpy(app->name_buffer, furi_string_get_cstr(name), sizeof(app->name_buffer) - 1);
    app->name_buffer[sizeof(app->name_buffer) - 1] = '\0';
    strncpy(app->secret_buffer, furi_string_get_cstr(secret), sizeof(app->secret_buffer) - 1);
    app->secret_buffer[sizeof(app->secret_buffer) - 1] = '\0';
    app->put_digits = (uint8_t)digits;
    app->cli_put_busy = true;
    furi_mutex_release(app->cli_mutex);

    /* Drop local secret copy before returning to the shell. */
    furi_string_free(name);
    furi_string_reset(secret);
    furi_string_free(secret);

    view_dispatcher_send_custom_event(app->view_dispatcher, TotpCustomEventCliPut);
    printf(
        "OK: armed PUT name=\"%s\" digits=%u — present card on Flipper\n",
        app->name_buffer,
        (unsigned)digits);
}

void totp_cli_register(TotpApp* app) {
    furi_assert(app);
    CliRegistry* cli = furi_record_open(RECORD_CLI);
    cli_registry_add_command(
        cli, TOTP_CLI_PUT_CMD, CliCommandFlagParallelSafe, totp_cli_put_callback, app);
    furi_record_close(RECORD_CLI);
}

void totp_cli_unregister(TotpApp* app) {
    UNUSED(app);
    CliRegistry* cli = furi_record_open(RECORD_CLI);
    cli_registry_delete_command(cli, TOTP_CLI_PUT_CMD);
    furi_record_close(RECORD_CLI);
}
