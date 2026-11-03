#ifndef FALAFEL_MAIL_LINK_H
#define FALAFEL_MAIL_LINK_H
#include "falafel_mailbox.h"
#define FALAFEL_MAIL_FRAME_HEADER 30u
#define FALAFEL_MAIL_FRAME_MAX 286u
#define FALAFEL_MAIL_DATA 4u
#define FALAFEL_MAIL_RECEIPT 5u
typedef struct {
    uint8_t kind, id[16];
    uint32_t source, destination;
    const uint8_t *envelope;
    size_t size;
} falafel_mail_frame;
/* Frame codec excludes the existing TCP 4-byte length prefix. */
int falafel_mail_frame_encode(const falafel_mail_frame *, uint8_t out[286], size_t *);
int falafel_mail_frame_decode(const uint8_t *, size_t, falafel_mail_frame *);
typedef int (*falafel_mail_send_fn)(void *, const uint8_t *, size_t);
typedef struct {
    falafel_mailbox *mailbox;
    uint32_t own, peer;
    uint64_t due[8];
    unsigned ack_mask, cursor;
    falafel_mail_send_fn send;
    void *user;
    falafel_mail_result last_receive;
    uint8_t retired_ack[8][172];
    unsigned retired_ack_count;
} falafel_mail_link;
void falafel_mail_link_init(falafel_mail_link *, falafel_mailbox *, uint32_t, uint32_t,
                           falafel_mail_send_fn, void *);
/* Call on each new TCP connection to recover any saved receipts. */
void falafel_mail_link_connected(falafel_mail_link *);
falafel_mail_result falafel_mail_link_receive(falafel_mail_link *, const uint8_t *, size_t);
/* Call only while connected; one bounded frame per tick, false send means busy.
 * Retry timers do not change mailbox state; only verified receipts do. */
void falafel_mail_link_tick(falafel_mail_link *, uint64_t now_ms);
/* Effect callback must durably and idempotently apply (paired sender, ID).
 * Returning nonzero leaves message available. No exactly-once external claim. */
typedef int (*falafel_mail_effect_fn)(void *, const uint8_t sender[32],
                                    const uint8_t id[16], const uint8_t *, size_t);
falafel_mail_result falafel_mail_link_consume(falafel_mail_link *, falafel_mail_effect_fn, void *);
/* Local retirement, retaining exact ACK copies for fair immediate sending. */
falafel_mail_result falafel_mail_link_reclaim(falafel_mail_link *);
#endif
