#include "lab_messaging.h"
#include <string.h>

static void notify(falafel_lab *s, falafel_lab_event event, const uint8_t id[16])
{ if (s->notify) s->notify(s->user, event, id); }

void falafel_lab_close(falafel_lab *s)
{
    if (!s) return;
    if (s->initialized) falafel_crypto_free(&s->crypto);
    falafel_crypto_secure_zero(s, sizeof(*s));
}

falafel_mail_result falafel_lab_open(falafel_lab *s, falafel_mail_storage storage,
    const uint8_t credential[64], uint32_t own, uint32_t peer, bool create,
    const char *preset, falafel_mail_send_fn send, void *send_user,
    falafel_lab_notify event, void *event_user)
{
    if (!s || !credential || !preset || !*preset || strlen(preset) > 117 ||
        !send || !own || !peer || own == peer || own == UINT32_MAX || peer == UINT32_MAX)
        return FALAFEL_MAIL_INVALID;
    memset(s, 0, sizeof(*s));
    if (falafel_crypto_init(&s->crypto)) return FALAFEL_MAIL_AUTH;
    s->initialized = true; s->notify = event; s->user = event_user;
    memcpy(s->identity.local_private, credential, 32);
    memcpy(s->identity.peer_public, credential + 32, 32);
    s->identity.has_pairing = 1; s->identity.generation = 1;
    if (falafel_crypto_x25519_public_from_private(&s->crypto, credential, 32,
            s->identity.local_public, 32)) return FALAFEL_MAIL_AUTH;
    if (falafel_crypto_constant_time_equal(s->identity.local_public, s->identity.peer_public, 32))
        return FALAFEL_MAIL_INVALID;
    falafel_mail_result result = falafel_mail_open(&s->mailbox, storage,
                                                 &s->crypto, &s->identity, create);
    if (result) return result;
    /* This bounded lab store deliberately does not migrate simulator archives. */
    if (s->mailbox.format_version != 1) { s->mailbox.ready = 0; return FALAFEL_MAIL_INVALID; }
    falafel_mail_link_init(&s->link, &s->mailbox, own, peer, send, send_user);
    bool used = false;
    for (unsigned i = 0; i < 8; ++i) if (s->mailbox.outbox[i].state) {
        used = true; notify(s, LAB_RESTORED, s->mailbox.outbox[i].envelope + 70);
    }
    if (!used) {
        uint8_t id[16];
        if (falafel_crypto_random(&s->crypto, id, sizeof(id))) return FALAFEL_MAIL_AUTH;
        result = falafel_mail_enqueue(&s->mailbox, id, (const uint8_t *)preset, strlen(preset));
        if (result) return result;
        notify(s, LAB_STORED, id);
    }
    return FALAFEL_MAIL_OK;
}

bool falafel_lab_receive(void *user, const uint8_t *wire, size_t size)
{
    falafel_lab *s = user;
    falafel_mail_frame frame;
    if (!s || !s->mailbox.ready || falafel_mail_frame_decode(wire, size, &frame)) return false;
    uint8_t before = 0;
    for (unsigned i = 0; i < 8; ++i)
        if (s->mailbox.outbox[i].state && !memcmp(s->mailbox.outbox[i].envelope + 70, frame.id, 16))
            before = s->mailbox.outbox[i].state;
    falafel_mail_result result = falafel_mail_link_receive(&s->link, wire, size);
    if (result != FALAFEL_MAIL_OK && result != FALAFEL_MAIL_DUPLICATE) {
        notify(s, LAB_REJECTED, frame.id);
        return s->mailbox.ready != 0;
    }
    if (frame.kind == FALAFEL_MAIL_RECEIPT && before == 1 && result == FALAFEL_MAIL_OK)
        notify(s, LAB_DELIVERED, frame.id); /* ONLY after authenticated receipt + durable commit */
    /* The only local application effect is a diagnostic ID event. Never log
     * plaintext. Consumption itself persists; no external exactly-once claim. */
    for (unsigned i = 0; i < 8; ++i) if (s->mailbox.inbox[i].state == 1) {
        uint8_t id[16]; memcpy(id, s->mailbox.inbox[i].envelope + 70, 16);
        if (falafel_mail_consume(&s->mailbox, id)) return false;
        notify(s, LAB_RECEIVED, id);
    }
    return true;
}
