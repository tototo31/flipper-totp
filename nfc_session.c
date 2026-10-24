#include "nfc_session.h"

#include <furi.h>

#include <string.h>

#define TAG "NfcSession"

#define NFC_SESSION_SETTLE_MS 40
#define NFC_SESSION_MAX_TRANSPORT_FAILS 6

struct NfcSession {
    Nfc* nfc;
    NfcPoller* poller;
    NfcSessionCallback callback;
    void* callback_context;

    NfcSessionOp op;
    char password[TOTP_PREFS_PASSWORD_MAX];
    char name[YKOATH_NAME_MAX];
    YkoathPutParams put;
    uint64_t unix_utc;

    YkoathSelectInfo select;
    YkoathCredentialList credentials;
    YkoathResult result;
    bool running;
    bool card_seen;
    uint8_t transport_fails;
};

NfcSession* nfc_session_alloc(void) {
    NfcSession* session = malloc(sizeof(NfcSession));
    memset(session, 0, sizeof(NfcSession));
    session->nfc = nfc_alloc();
    session->result = YkoathOk;
    session->op = NfcSessionOpReadAll;
    return session;
}

void nfc_session_free(NfcSession* session) {
    furi_assert(session);
    nfc_session_stop(session);
    nfc_free(session->nfc);
    free(session);
}

void nfc_session_set_callback(NfcSession* session, NfcSessionCallback callback, void* context) {
    furi_assert(session);
    session->callback = callback;
    session->callback_context = context;
}

void nfc_session_set_op(NfcSession* session, NfcSessionOp op) {
    furi_assert(session);
    session->op = op;
}

void nfc_session_set_name(NfcSession* session, const char* name) {
    furi_assert(session);
    session->name[0] = '\0';
    if(name) {
        strncpy(session->name, name, sizeof(session->name) - 1);
    }
}

void nfc_session_set_put(NfcSession* session, const YkoathPutParams* params) {
    furi_assert(session && params);
    session->put = *params;
}

static YkoathResult nfc_session_run_op(NfcSession* session, Iso14443_4aPoller* poller) {
    const char* password = session->password[0] ? session->password : NULL;
    YkoathResult r = ykoath_open(poller, password, &session->select);
    if(r != YkoathOk) return r;

    switch(session->op) {
    case NfcSessionOpList:
        return ykoath_list(poller, &session->credentials);
    case NfcSessionOpCalculate: {
        YkoathCredential cred;
        r = ykoath_calculate(poller, session->name, session->unix_utc, &cred);
        if(r != YkoathOk) return r;
        session->credentials.count = 1;
        session->credentials.items[0] = cred;
        return YkoathOk;
    }
    case NfcSessionOpPut:
        return ykoath_put(poller, &session->put);
    case NfcSessionOpDelete:
        return ykoath_delete(poller, session->name);
    case NfcSessionOpReadAll:
    default:
        return ykoath_calculate_all(poller, session->unix_utc, &session->credentials);
    }
}

static void nfc_session_fail_and_stop(NfcSession* session) {
    if(session->callback) {
        session->callback(NfcSessionEventError, session->callback_context);
    }
}

static NfcCommand nfc_session_poller_callback(NfcGenericEvent event, void* context) {
    NfcSession* session = context;

    if(event.protocol != NfcProtocolIso14443_4a) {
        return NfcCommandContinue;
    }

    const Iso14443_4aPollerEvent* iso_event = event.event_data;
    if(iso_event->type == Iso14443_4aPollerEventTypeError) {
        Iso14443_4aError err = iso_event->data->error;
        /*
         * Iso14443_4aErrorNone (== 0) is not a failure — firmware sometimes
         * delivers Error events with None. Logging them at Error flooded the
         * console every ~200ms while waiting for a card and forced needless
         * Reset (did not fail PUT, just spam + thrash).
         */
        if(err == Iso14443_4aErrorNone) {
            return NfcCommandContinue;
        }
        /* Idle field: no card / probe timeout — expected while Place card. */
        if(err == Iso14443_4aErrorNotPresent || err == Iso14443_4aErrorTimeout) {
            session->card_seen = false;
            session->transport_fails = 0;
            FURI_LOG_D(TAG, "waiting for card (%d)", err);
            return NfcCommandReset;
        }
        FURI_LOG_W(TAG, "poller error %d", err);
        session->card_seen = false;
        session->transport_fails = 0;
        return NfcCommandReset;
    }
    if(iso_event->type != Iso14443_4aPollerEventTypeReady) {
        return NfcCommandContinue;
    }

    if(!session->card_seen) {
        session->card_seen = true;
        session->transport_fails = 0;
        if(session->callback) {
            session->callback(NfcSessionEventCardDetected, session->callback_context);
        }
    }

    /* Settle only on the first tries; keep retries snappy. */
    if(session->transport_fails == 0) {
        furi_delay_ms(NFC_SESSION_SETTLE_MS);
    } else {
        furi_delay_ms(10);
    }

    memset(&session->select, 0, sizeof(session->select));
    memset(&session->credentials, 0, sizeof(session->credentials));
    session->result = nfc_session_run_op(session, event.instance);

    if(session->result == YkoathOk) {
        session->transport_fails = 0;
        if(session->callback) {
            session->callback(NfcSessionEventSuccess, session->callback_context);
        }
        return NfcCommandStop;
    }

    if(session->result == YkoathErrorNeedPin) {
        if(session->callback) {
            session->callback(NfcSessionEventNeedPin, session->callback_context);
        }
        return NfcCommandStop;
    }

    if(session->result == YkoathErrorTransport) {
        session->transport_fails++;
        FURI_LOG_W(TAG, "transport fail #%u", session->transport_fails);
        if(session->transport_fails >= NFC_SESSION_MAX_TRANSPORT_FAILS) {
            /* Give up this card attempt so UI can leave "Hold steady…". */
            nfc_session_fail_and_stop(session);
            return NfcCommandStop;
        }
        /* Re-activate; same or next card can succeed on a later Ready. */
        return NfcCommandReset;
    }

    /* SELECT SW / auth / parse — fail immediately (e.g. no OATH applet). */
    nfc_session_fail_and_stop(session);
    return NfcCommandStop;
}

void nfc_session_start(NfcSession* session, const char* password, uint64_t unix_utc) {
    furi_assert(session);
    nfc_session_stop(session);

    memset(&session->select, 0, sizeof(session->select));
    memset(&session->credentials, 0, sizeof(session->credentials));
    session->result = YkoathOk;
    session->unix_utc = unix_utc;
    session->card_seen = false;
    session->transport_fails = 0;
    session->password[0] = '\0';
    if(password) {
        strncpy(session->password, password, sizeof(session->password) - 1);
    }

    session->poller = nfc_poller_alloc(session->nfc, NfcProtocolIso14443_4a);
    session->running = true;
    nfc_poller_start(session->poller, nfc_session_poller_callback, session);
}

void nfc_session_stop(NfcSession* session) {
    furi_assert(session);
    if(session->poller) {
        nfc_poller_stop(session->poller);
        nfc_poller_free(session->poller);
        session->poller = NULL;
    }
    session->running = false;
}

bool nfc_session_is_running(const NfcSession* session) {
    furi_assert(session);
    return session->running;
}

void nfc_session_clear_put(NfcSession* session) {
    furi_assert(session);
    memset(&session->put, 0, sizeof(session->put));
}

NfcSessionOp nfc_session_get_op(const NfcSession* session) {
    return session->op;
}

const YkoathCredentialList* nfc_session_get_credentials(const NfcSession* session) {
    return &session->credentials;
}

const YkoathSelectInfo* nfc_session_get_select(const NfcSession* session) {
    return &session->select;
}

YkoathResult nfc_session_get_result(const NfcSession* session) {
    return session->result;
}

const char* nfc_session_result_str(YkoathResult result) {
    return ykoath_result_str(result);
}
