#include "falafel_storage_espidf.h"
#include "nvs_model.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Fresh disposable identities, generated in process RAM only. Never deployed or
 * saved as identity files, and never used to contact a relay. Host Mbed TLS. */
static falafel_crypto_context_t crypto;
static falafel_pairing_persistent_state identities[2];
static falafel_storage_espidf store;
static falafel_history history;
static falafel_mailbox box, sender;
static uint8_t sender_snapshot[FALAFEL_MAILBOX_MAX_SNAPSHOT_SIZE];
static size_t sender_snapshot_size;
static uint8_t wire[256], receipt[256], message_id[16];
static size_t wire_size, receipt_size;
static const uint8_t text[] = "disposable-storage-test-private-text";

static void id_for(uint8_t id[16], unsigned n)
{ memset(id, 0xa5, 16); for (unsigned i = 0; i < 4; ++i) id[15 - i] = (uint8_t)(n >> (i * 8)); }
static int sender_load(void *u, uint8_t *out, size_t cap, size_t *n)
{
    (void)u; if (!sender_snapshot_size) return 1;
    assert(cap >= sender_snapshot_size); memcpy(out, sender_snapshot, sender_snapshot_size);
    *n = sender_snapshot_size; return 0;
}
static int sender_replace(void *u, const uint8_t *data, size_t n)
{ (void)u; assert(n <= sizeof(sender_snapshot)); memcpy(sender_snapshot, data, n); sender_snapshot_size = n; return 0; }
static void close_store(void)
{ falafel_history_clear(&history); assert(falafel_storage_espidf_close(&store) == ESP_OK); }
static void open_store(bool create)
{
    assert(falafel_storage_espidf_open(&store, "falafel_data", create) == ESP_OK);
    assert(falafel_history_open(&history, falafel_storage_espidf_history(&store),
        &crypto, &identities[0], create) == FALAFEL_HISTORY_OK);
    assert(falafel_mail_open(&box, falafel_storage_espidf_mailbox(&store),
        &crypto, &identities[0], create) == FALAFEL_MAIL_OK);
    assert(falafel_mail_set_retention(&box, falafel_history_retention(&history)) == FALAFEL_MAIL_OK);
    if (box.format_version == 1) assert(falafel_mail_upgrade_retention(&box) == FALAFEL_MAIL_OK);
}
static void fresh(void) { fake_nvs_reset(); open_store(true); }
static void reboot_store(void) { close_store(); fake_nvs_reboot(); open_store(false); }
static void base_message(falafel_history_message *m, unsigned direction, const uint8_t id[16])
{
    memset(m, 0, sizeof(*m)); memcpy(m->id, id, 16); m->direction = (uint8_t)direction;
    unsigned from = direction ? 1 : 0, to = 1 - from;
    memcpy(m->sender, identities[from].local_public, 32);
    memcpy(m->recipient, identities[to].local_public, 32);
    m->source = from + 1; m->destination = to + 1;
    m->size = sizeof(text) - 1; memcpy(m->text, text, m->size);
}
static void make_wire(unsigned n)
{
    sender_snapshot_size = 0;
    falafel_mail_storage io = {NULL, sender_load, sender_replace};
    assert(falafel_mail_open(&sender, io, &crypto, &identities[1], 1) == FALAFEL_MAIL_OK);
    id_for(message_id, n);
    assert(falafel_mail_enqueue(&sender, message_id, text, sizeof(text) - 1) == FALAFEL_MAIL_OK);
    assert(falafel_mail_retry(&sender, 0, wire, &wire_size) == FALAFEL_MAIL_OK);
}
static void receive(void)
{ assert(falafel_mail_receive(&box, wire, wire_size, receipt, &receipt_size) == FALAFEL_MAIL_OK); }
static int save_consume_reclaim(void)
{
    for (unsigned i = 0; i < 8; ++i) {
        if (box.inbox[i].state != 1) continue;
        uint8_t id[16], plain[117]; size_t n;
        assert(falafel_mail_peek(&box, id, plain, &n) == FALAFEL_MAIL_OK);
        falafel_history_message m; base_message(&m, 1, id);
        assert(n == m.size && !memcmp(plain, m.text, n));
        m.mail = box.inbox[i]; m.delivery = FALAFEL_HISTORY_ACCEPTED;
        /* The production coordinator must preserve precisely this ordering. */
        if (falafel_history_put(&history, &m) != FALAFEL_HISTORY_OK) return -1;
        if (falafel_mail_consume(&box, id) != FALAFEL_MAIL_OK) return -1;
    }
    return falafel_mail_reclaim(&box) == FALAFEL_MAIL_OK ? 0 : -1;
}
static void duplicate_receipt(void)
{
    uint8_t ack[256]; size_t n;
    assert(falafel_mail_receive(&box, wire, wire_size, ack, &n) == FALAFEL_MAIL_DUPLICATE);
    assert(n == receipt_size && !memcmp(ack, receipt, n));
}

static void basic(void)
{
    fake_nvs_reset();
    assert(falafel_storage_espidf_open(&store, "nvs", true) == ESP_ERR_INVALID_ARG);
    assert(falafel_storage_espidf_open(&store, "falafel_data", false) == ESP_ERR_NVS_NOT_FOUND);
    assert(fake_nvs_keys() == 0);
    open_store(true);
    falafel_storage_espidf another = {0};
    assert(falafel_storage_espidf_open(&another, "other", true) == ESP_ERR_INVALID_STATE);
    assert(falafel_storage_espidf_open(&store, "falafel_data", true) == ESP_ERR_INVALID_STATE);
    uint8_t id[16]; id_for(id, 1); falafel_history_message m, got;
    base_message(&m, 0, id);
    assert(falafel_history_put(&history, &m) == FALAFEL_HISTORY_OK);
    unsigned writes = fake_nvs_writes();
    assert(falafel_history_put(&history, &m) == FALAFEL_HISTORY_OK);
    assert(history.count == 1 && writes == fake_nvs_writes());
    m.read = 1;
    assert(falafel_history_put(&history, &m) == FALAFEL_HISTORY_OK);
    m.read = 0;
    assert(falafel_history_put(&history, &m) == FALAFEL_HISTORY_OK);
    reboot_store();
    assert(falafel_history_get(&history, 0, id, &got) == FALAFEL_HISTORY_OK);
    assert(got.read && got.size == sizeof(text) - 1 && !memcmp(got.text, text, got.size));
    m.text[0] ^= 1;
    assert(falafel_history_put(&history, &m) == FALAFEL_HISTORY_CONFLICT);
    size_t n; const uint8_t *bytes = fake_nvs_blob("r0000", &n);
    assert(bytes && n == 800 && !memcmp(bytes, "FHR1", 4));
    for (size_t i = 0; i + sizeof(text) - 1 <= n; ++i)
        assert(memcmp(bytes + i, text, sizeof(text) - 1));
    bytes = fake_nvs_blob("mailbox", &n); assert(bytes && n == 8392 && !memcmp(bytes, "FMBX\2", 5));
    bytes = fake_nvs_blob("index", &n); assert(bytes && n == 77 && !memcmp(bytes, "FHI1", 4));
    close_store();
    puts("storage basic: exact formats, explicit creation, exclusive owner, idempotence, read restart, ciphertext PASS");
}

static void messages(void)
{
    fresh();
    for (unsigned i = 1; i <= 100; ++i) {
        make_wire(i); receive(); assert(!save_consume_reclaim());
        duplicate_receipt();
        reboot_store(); duplicate_receipt();
        assert(history.count == i);
        for (unsigned j = 0; j < 8; ++j) assert(!box.inbox[j].state);
    }
    /* Same full ID in the other direction is a distinct logical record. */
    falafel_history_message m; base_message(&m, 0, message_id);
    assert(falafel_history_put(&history, &m) == FALAFEL_HISTORY_OK && history.count == 101);
    reboot_store(); assert(history.count == 101);
    close_store();
    puts("storage integration: 100 encrypted receives/restarts, safe consume/reclaim, exact duplicate ACKs PASS");
}

static void interruptions(void)
{
    unsigned scenarios = 0;
    for (unsigned ordinal = 1; ordinal <= 5; ++ordinal) {
        for (enum fake_cut cut = FAKE_BEFORE_SET; cut <= FAKE_AFTER_COMMIT; ++cut) {
            fresh(); make_wire(1); receive();
            fake_nvs_fault(ordinal, cut);
            assert(save_consume_reclaim() == -1 && store.poisoned);
            /* Closed poisoned handle refuses even otherwise-valid reads. */
            uint8_t buf[9000]; size_t n;
            falafel_mail_storage io = falafel_storage_espidf_mailbox(&store);
            assert(io.load(io.user, buf, sizeof(buf), &n) == -1);
            reboot_store();
            falafel_history_message got;
            falafel_history_result r = falafel_history_get(&history, 1, message_id, &got);
            if (r == FALAFEL_HISTORY_ABSENT) {
                /* If no durable effect exists, the mailbox MUST remain available. */
                assert(box.inbox[0].state == 1);
            } else assert(r == FALAFEL_HISTORY_OK);
            assert(!save_consume_reclaim());
            assert(falafel_history_get(&history, 1, message_id, &got) == FALAFEL_HISTORY_OK);
            assert(got.mail.state == 2 && history.count == 1);
            duplicate_receipt(); reboot_store(); duplicate_receipt();
            close_store(); ++scenarios;
        }
    }
    /* Inbox acceptance itself can fail before or after its publication. No
     * receipt is returned on failure; exact retransmission safely completes. */
    for (enum fake_cut cut = FAKE_BEFORE_SET; cut <= FAKE_AFTER_COMMIT; ++cut) {
        fresh(); make_wire(1); fake_nvs_fault(1, cut);
        uint8_t ack[256]; size_t n;
        assert(falafel_mail_receive(&box, wire, wire_size, ack, &n) == FALAFEL_MAIL_IO);
        reboot_store();
        falafel_mail_result r = falafel_mail_receive(&box, wire, wire_size, receipt, &receipt_size);
        assert(r == FALAFEL_MAIL_OK || r == FALAFEL_MAIL_DUPLICATE);
        assert(!save_consume_reclaim()); duplicate_receipt(); close_store(); ++scenarios;
    }
    printf("storage interruptions: %u modeled OLD/NEW/fragment/error scenarios, orphan recovery and no premature consume PASS\n", scenarios);
}

static void corruption(void)
{
    for (unsigned kind = 0; kind < 7; ++kind) {
        fresh(); uint8_t id[16]; id_for(id, 1);
        falafel_history_message m; base_message(&m, 0, id);
        assert(falafel_history_put(&history, &m) == FALAFEL_HISTORY_OK);
        close_store();
        if (kind == 0) fake_nvs_flip("r0000", 80); /* encrypted body */
        if (kind == 1) fake_nvs_flip("index", 45); /* encrypted index */
        if (kind == 2) fake_nvs_flip("mailbox", 100); /* checksum failure */
        if (kind == 3) fake_nvs_delete("index");
        if (kind == 4) fake_nvs_delete("r0000");
        if (kind == 5) fake_nvs_flip("r0000", 21); /* authenticated routing header */
        if (kind == 6) {
            size_t n; const uint8_t *p = fake_nvs_blob("r0000", &n);
            fake_nvs_raw("r0001", NVS_TYPE_BLOB, p, n); /* alias same full ID */
        }
        unsigned writes = fake_nvs_writes();
        esp_err_t error = falafel_storage_espidf_open(&store, "falafel_data", true);
        if (kind == 6) assert(error != ESP_OK);
        else {
            assert(error == ESP_OK);
            if (kind == 2) {
                assert(falafel_mail_open(&box, falafel_storage_espidf_mailbox(&store),
                    &crypto, &identities[0], 1) == FALAFEL_MAIL_CORRUPT);
            } else {
                assert(falafel_history_open(&history, falafel_storage_espidf_history(&store),
                    &crypto, &identities[0], 1) == FALAFEL_HISTORY_CORRUPT);
            }
            close_store();
        }
        assert(fake_nvs_writes() == writes); /* even explicit create did not reset it */
    }
    /* Structurally malformed namespace, truncated blob, type mismatch. */
    for (unsigned kind = 0; kind < 3; ++kind) {
        fake_nvs_reset(); uint8_t data[800] = {0};
        if (kind == 0) fake_nvs_raw("unexpected", NVS_TYPE_BLOB, data, sizeof(data));
        if (kind == 1) fake_nvs_raw("r0000", NVS_TYPE_BLOB, data, 400);
        if (kind == 2) fake_nvs_raw("index", NVS_TYPE_U8, data, 1);
        assert(falafel_storage_espidf_open(&store, "falafel_data", true) != ESP_OK);
        assert(fake_nvs_keys() == 1);
    }
    fresh(); close_store(); unsigned keys = fake_nvs_keys();
    esp_err_t errors[] = {ESP_ERR_NVS_NO_FREE_PAGES, ESP_ERR_NVS_NEW_VERSION_FOUND, ESP_FAIL};
    for (unsigned i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        fake_nvs_init_error(errors[i]);
        assert(falafel_storage_espidf_open(&store, "falafel_data", true) == errors[i]);
        assert(fake_nvs_keys() == keys);
    }
    fake_nvs_init_error(ESP_OK); open_store(false); close_store();
    puts("storage corruption: GCM/checksum/missing records/header/aliases/types/init errors fail closed, no reset PASS");
}

static void full(void)
{
    fresh(); make_wire(1); receive();
    fake_nvs_capacity(0);
    assert(save_consume_reclaim() == -1 && box.inbox[0].state == 1);
    assert(store.last_error == ESP_ERR_NVS_NOT_ENOUGH_SPACE);
    fake_nvs_capacity(2 * 1024 * 1024); reboot_store();
    assert(box.inbox[0].state == 1 && !save_consume_reclaim()); close_store();

    fresh(); uint8_t id[16];
    for (unsigned i = 1; i <= FALAFEL_HISTORY_CAPACITY; ++i) {
        id_for(id, i); falafel_history_message m; base_message(&m, 0, id);
        assert(falafel_history_put(&history, &m) == FALAFEL_HISTORY_OK);
    }
    unsigned writes = fake_nvs_writes();
    id_for(id, 2000); falafel_history_message m; base_message(&m, 0, id);
    assert(falafel_history_put(&history, &m) == FALAFEL_HISTORY_FULL);
    assert(fake_nvs_writes() == writes && history.count == 1024);
    id_for(id, 1); assert(falafel_history_get(&history, 0, id, &m) == FALAFEL_HISTORY_OK);
    m.read = 1; assert(falafel_history_put(&history, &m) == FALAFEL_HISTORY_OK);
    reboot_store(); assert(history.count == 1024);
    assert(falafel_history_get(&history, 0, id, &m) == FALAFEL_HISTORY_OK && m.read);
    size_t n; assert(fake_nvs_blob("r1023", &n) && n == 800);
    assert(fake_nvs_blob("index", &n) && n == 17468);
    assert(fake_nvs_keys() == 1026); close_store();

    fresh();
    for (unsigned i = 0; i < 8; ++i) {
        id_for(id, i); assert(falafel_mail_enqueue(&box, id, text, sizeof(text) - 1) == FALAFEL_MAIL_OK);
    }
    id_for(id, 9); assert(falafel_mail_enqueue(&box, id, text, sizeof(text) - 1) == FALAFEL_MAIL_FULL);
    reboot_store();
    for (unsigned i = 0; i < 8; ++i) assert(box.outbox[i].state == 1);
    close_store();
    puts("storage full: modeled flash ENOSPC, 1024 records/no eviction, existing updates, eight active slots PASS");
}

int main(void)
{
    assert(falafel_crypto_init(&crypto) == FALAFEL_CRYPTO_OK);
    for (unsigned i = 0; i < 2; ++i) {
        identities[i].has_pairing = 1; identities[i].generation = 1;
        assert(!falafel_crypto_x25519_generate(&crypto, identities[i].local_private, 32, identities[i].local_public, 32));
    }
    memcpy(identities[0].peer_public, identities[1].local_public, 32);
    memcpy(identities[1].peer_public, identities[0].local_public, 32);
    basic(); messages(); interruptions(); corruption(); full();
    printf("storage RAM: backend=%zu history=%zu mailbox=%zu bytes (host ABI; excludes NVS)\n",
        sizeof(store), sizeof(history), sizeof(box));
    fake_nvs_reset(); falafel_crypto_free(&crypto);
    falafel_crypto_secure_zero(identities, sizeof(identities));
    return 0;
}
