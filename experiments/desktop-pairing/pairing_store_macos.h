#ifndef PAIRING_STORE_MACOS_H
#define PAIRING_STORE_MACOS_H

#include "falafel_pairing_persistence.h"

typedef struct {
    const char *metadata_path;
    const char *keychain_service;
    const char *keychain_account;
} falafel_pairing_macos_store;

enum {
    FALAFEL_STORE_OK = 0,
    FALAFEL_STORE_ERROR = -1,
    FALAFEL_STORE_CORRUPT = -2
};

int falafel_pairing_macos_load_or_create(const falafel_pairing_macos_store *store,
                                         falafel_pairing_persistent_state *state);
int falafel_pairing_macos_save(const falafel_pairing_macos_store *store,
                               const falafel_pairing_persistent_state *state);
int falafel_pairing_macos_unpair(const falafel_pairing_macos_store *store,
                                 int rotate_identity,
                                 falafel_pairing_persistent_state *state);
int falafel_pairing_macos_destroy(const falafel_pairing_macos_store *store);

/* Fault-injection hook: writes and fsyncs a temporary file but never renames it. */
int falafel_pairing_macos_simulate_interrupted_save(
    const falafel_pairing_macos_store *store,
    const falafel_pairing_persistent_state *state);

#endif
