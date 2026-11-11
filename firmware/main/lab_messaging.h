#pragma once
#include "falafel_mail_link.h"
#include <stdbool.h>

/* Lab worker state; caller serializes all calls. Uses the existing mailbox,
 * without retirement: eight lifetime IDs/direction. Never a second ACK FSM. */
typedef enum {
    LAB_STORED, LAB_RECEIVED, LAB_DELIVERED, LAB_RESTORED, LAB_REJECTED
} falafel_lab_event;
typedef void (*falafel_lab_notify)(void *, falafel_lab_event, const uint8_t id[16]);
typedef struct {
    falafel_crypto_context_t crypto;
    falafel_pairing_persistent_state identity;
    falafel_mailbox mailbox;
    falafel_mail_link link;
    falafel_lab_notify notify;
    void *user;
    bool initialized;
} falafel_lab;

/* credential = local private key || paired public key, same 64B fixture format
 * as the simulator. create is explicit; existing stores are never reset.
 * Creates exactly one automatic outgoing preset only if outbox never used. */
falafel_mail_result falafel_lab_open(falafel_lab *, falafel_mail_storage,
    const uint8_t credential[64], uint32_t own, uint32_t peer, bool create,
    const char *preset, falafel_mail_send_fn, void *send_user,
    falafel_lab_notify, void *notify_user);
bool falafel_lab_receive(void *, const uint8_t *, size_t);
void falafel_lab_close(falafel_lab *);
