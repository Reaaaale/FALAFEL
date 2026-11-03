#pragma once
#include "falafel_mailbox.h"
#define FALAFEL_HISTORY_CAPACITY 1024u
#define FALAFEL_HISTORY_RECORD_BYTES 800u
#define FALAFEL_HISTORY_INDEX_BYTES (60u + 17u * FALAFEL_HISTORY_CAPACITY)
enum { FALAFEL_HISTORY_OUT=0, FALAFEL_HISTORY_IN=1 };
enum { FALAFEL_HISTORY_PENDING=0, FALAFEL_HISTORY_QUEUED=1, FALAFEL_HISTORY_ACCEPTED=2 };
typedef enum { FALAFEL_HISTORY_OK=0, FALAFEL_HISTORY_ABSENT, FALAFEL_HISTORY_FULL,
    FALAFEL_HISTORY_INVALID, FALAFEL_HISTORY_IO, FALAFEL_HISTORY_CORRUPT,
    FALAFEL_HISTORY_CONFLICT } falafel_history_result;
typedef struct {
    uint8_t id[16], sender[32], recipient[32];
    uint32_t source, destination;
    uint64_t sequence;
    uint8_t direction, delivery, read, text_missing;
    uint8_t size;
    uint8_t text[117];
    falafel_mail_record mail;
} falafel_history_message;
typedef int (*falafel_history_visit)(void *, const char *name);
typedef struct {
    void *user;
    int (*load)(void *, const char *, uint8_t *, size_t, size_t *); /* 0/1/-1 */
    int (*replace)(void *, const char *, const uint8_t *, size_t); /* durable atomic */
    int (*scan)(void *, falafel_history_visit, void *); /* committed *.msg only */
} falafel_history_storage;
/* Last three fields are volatile cache; index wire entries stay 17 bytes. */
typedef struct { uint8_t direction, id[16], delivery, mail_state, read; } falafel_history_key;
typedef struct {
    falafel_history_storage storage;
    falafel_crypto_context_t *crypto;
    const falafel_pairing_persistent_state *identity;
    uint8_t key[32];
    uint8_t archive_id[16];
    falafel_history_key index[FALAFEL_HISTORY_CAPACITY];
    unsigned count;
    int ready;
    uint8_t plain[FALAFEL_HISTORY_INDEX_BYTES], wire[FALAFEL_HISTORY_INDEX_BYTES];
} falafel_history;
/* Explicit create only for a new empty namespace. Existing missing/corrupt
 * manifests fail closed. Authenticated orphan records from interrupted index
 * publication are recovered. Calls serialized by owner, storage globally locked.
 * AES256-GCM uses fresh random nonces, HKDF(local private, paired public keys,
 * dedicated history domain). Private-key protection/backup is a platform duty.
 * Capacity is lifetime archived IDs, not an eviction window; FULL is explicit.
 * Authenticated old-state rollback/deletion of the entire store is not prevented. */
falafel_history_result falafel_history_open(falafel_history *, falafel_history_storage,
    falafel_crypto_context_t *, const falafel_pairing_persistent_state *, int create);
falafel_history_result falafel_history_get(falafel_history *, unsigned direction,
    const uint8_t id[16], falafel_history_message *);
falafel_history_result falafel_history_at(falafel_history *, unsigned, falafel_history_message *);
/* Immutable sender/recipient/text/ID, monotonic delivery/read/mail state.
 * Same update is a no-op; write failure poisons handle until reopen. */
falafel_history_result falafel_history_put(falafel_history *, const falafel_history_message *);
falafel_mail_retention falafel_history_retention(falafel_history *);
void falafel_history_clear(falafel_history *);
