#pragma once
#include "falafel_crypto.h"
#include <stdbool.h>

#define SEC003_CORE_CASES 13u
typedef struct {
    void (*case_result)(void *, const char *, bool);
    void (*yield)(void *);
    void *user;
} sec003_hooks;
typedef struct {
    unsigned passed, failed;
    bool cleaned;
    /* SHA256 of two PUBLIC ephemeral keys, not a secret/key/DRBG output dump. */
    uint8_t public_boot_fingerprint[32];
} sec003_result;
/* Single-threaded experiment. Caller owns allocated context; no persistent I/O.
 * Executes the same portable test logic on host and device. */
bool sec003_suite_run(falafel_crypto_context_t *, const sec003_hooks *, sec003_result *);
