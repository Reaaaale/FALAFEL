#ifndef FALAFEL_MAILBOX_H
#define FALAFEL_MAILBOX_H
#include "falafel_async_e2ee.h"
#define FALAFEL_MAILBOX_CAPACITY 8u
#define FALAFEL_MAILBOX_MAX_TEXT 117u
#define FALAFEL_MAILBOX_RECORD_SIZE 517u
#define FALAFEL_MAILBOX_SNAPSHOT_SIZE (72u + 16u * FALAFEL_MAILBOX_RECORD_SIZE + 32u)
#define FALAFEL_MAILBOX_MAX_SNAPSHOT_SIZE (FALAFEL_MAILBOX_SNAPSHOT_SIZE + 16u)
typedef enum {
    FALAFEL_MAIL_OK = 0, FALAFEL_MAIL_DUPLICATE, FALAFEL_MAIL_FULL,
    FALAFEL_MAIL_INVALID, FALAFEL_MAIL_AUTH, FALAFEL_MAIL_IO,
    FALAFEL_MAIL_CORRUPT, FALAFEL_MAIL_NOT_FOUND, FALAFEL_MAIL_CONFLICT
} falafel_mail_result;
/* load: 0 success, 1 absent, -1 error. Never substitute empty for corrupt data.
 * replace: 0 only after durable atomic commit; failure may have committed.
 * A backend owns an exclusive lock for the entire mailbox lifetime.
 * Neither callback retains the buffer. Future NVS backend must honor this contract. */
typedef struct {
    void *user;
    int (*load)(void *, uint8_t *, size_t, size_t *);
    int (*replace)(void *, const uint8_t *, size_t);
} falafel_mail_storage;
typedef struct {
    uint8_t state; /* 0 vacant, 1 pending/available, 2 received/consumed */
    uint16_t size, ack_size;
    uint8_t envelope[256], ack[256];
} falafel_mail_record;
/* Optional durable retirement store. lookup: 0 found, 1 absent, -1 error.
 * put must retain the exact envelope AND saved receipt indefinitely. Never
 * discard retired IDs without a separately authenticated epoch design.
 * No network change; v2 snapshots add a 16-byte archive identity.
 * Borrowed for mailbox lifetime. */
typedef struct {
    void *user;
    int (*lookup)(void *, int outgoing, const uint8_t id[16], falafel_mail_record *);
    int (*put)(void *, int outgoing, const falafel_mail_record *);
    uint8_t archive_id[16];
} falafel_mail_retention;
typedef struct {
    falafel_mail_storage storage;
    const falafel_pairing_persistent_state *identity; /* borrowed, immutable */
    falafel_crypto_context_t *crypto; /* borrowed */
    int ready;
    falafel_mail_record outbox[8], inbox[8];
    uint8_t scratch[FALAFEL_MAILBOX_MAX_SNAPSHOT_SIZE];
    falafel_mail_retention retention;
    unsigned format_version; /* 1 legacy lifetime slots; 2 requires archive */
    uint8_t archive_id[16];
} falafel_mailbox;
/* Caller owns mailbox memory, identity and crypto for lifetime; serialize calls.
 * Creation must be explicitly requested only for a NEW mailbox. Missing existing
 * stores are errors. Failure of replace poisons this handle: reopen before use.
 * Default v1 has eight lifetime IDs/direction. Opt-in v2 retires completed
 * records only into durable retention; IDs/receipts are never evicted. Full
 * is backpressure, never automatic loss of replay history.
 * Buffers/arguments must not alias mailbox, identity or each other.
 */
falafel_mail_result falafel_mail_open(falafel_mailbox *, falafel_mail_storage,
    falafel_crypto_context_t *, const falafel_pairing_persistent_state *, int create);
falafel_mail_result falafel_mail_enqueue(falafel_mailbox *, const uint8_t id[16],
    const uint8_t *text, size_t size);
/* Copy pending exact wire bytes. Pending slots are stable until completion;
 * retired slots can be reused. Callers identify messages by ID, not old slots. */
falafel_mail_result falafel_mail_retry(falafel_mailbox *, unsigned slot,
    uint8_t wire[256], size_t *size);
/* Accept data durably, then return saved authenticated receipt. Duplicate exact
 * envelopes return the same saved ACK even after consumption or restart. */
falafel_mail_result falafel_mail_receive(falafel_mailbox *, const uint8_t *wire,
    size_t size, uint8_t ack[256], size_t *ack_size);
falafel_mail_result falafel_mail_ack(falafel_mailbox *, const uint8_t *, size_t);
/* At-least-once peek until consume. Effects outside this store are not atomic:
 * crash after effect before consume can repeat peek; consumer MUST deduplicate
 * effects by (paired sender key, ID) or transact them itself. No read receipt. */
falafel_mail_result falafel_mail_peek(falafel_mailbox *, uint8_t id[16],
    uint8_t text[FALAFEL_MAILBOX_MAX_TEXT], size_t *size);
falafel_mail_result falafel_mail_consume(falafel_mailbox *, const uint8_t id[16]);
/* Opt-in only after opening/validating a durable application archive. */
falafel_mail_result falafel_mail_set_retention(falafel_mailbox *, falafel_mail_retention);
/* Retire completed records before clearing their active slot. A crash between
 * archive and clear is harmless; failed clear poisons the handle as usual. */
falafel_mail_result falafel_mail_reclaim(falafel_mailbox *);
/* Explicit v1 -> v2 local snapshot migration, after archive is initialized and
 * all existing effects imported. Unchanged record encoding, no wire change.
 * A v2 handle rejects message operations until retention is attached. */
falafel_mail_result falafel_mail_upgrade_retention(falafel_mailbox *);
#endif
