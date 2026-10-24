#pragma once

#include "ykoath.h"
#include "prefs.h"

#include <furi.h>
#include <nfc/nfc.h>
#include <nfc/nfc_poller.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NfcSessionEventCardDetected,
    NfcSessionEventSuccess,
    NfcSessionEventNeedPin,
    NfcSessionEventError,
} NfcSessionEvent;

typedef enum {
    NfcSessionOpReadAll = 0,
    NfcSessionOpList,
    NfcSessionOpCalculate,
    NfcSessionOpPut,
    NfcSessionOpDelete,
} NfcSessionOp;

typedef void (*NfcSessionCallback)(NfcSessionEvent event, void* context);

typedef struct NfcSession NfcSession;

NfcSession* nfc_session_alloc(void);
void nfc_session_free(NfcSession* session);

void nfc_session_set_callback(NfcSession* session, NfcSessionCallback callback, void* context);

/**
 * Configure next NFC operation (call before start).
 * For Calculate/Delete: set_name. For Put: set_put.
 */
void nfc_session_set_op(NfcSession* session, NfcSessionOp op);
void nfc_session_set_name(NfcSession* session, const char* name);
void nfc_session_set_put(NfcSession* session, const YkoathPutParams* params);

/**
 * Start ISO14443-4A poller. APDU exchange runs inside the poller callback.
 */
void nfc_session_start(NfcSession* session, const char* password, uint64_t unix_utc);
void nfc_session_stop(NfcSession* session);
bool nfc_session_is_running(const NfcSession* session);
/** Zero PUT key material held in the session. */
void nfc_session_clear_put(NfcSession* session);

NfcSessionOp nfc_session_get_op(const NfcSession* session);
const YkoathCredentialList* nfc_session_get_credentials(const NfcSession* session);
const YkoathSelectInfo* nfc_session_get_select(const NfcSession* session);
YkoathResult nfc_session_get_result(const NfcSession* session);
const char* nfc_session_result_str(YkoathResult result);

#ifdef __cplusplus
}
#endif
