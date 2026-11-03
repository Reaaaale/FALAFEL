#include "falafel_e2ee.h"
#include <noise/protocol.h>
#include <stdlib.h>
#include <string.h>

#define SUITE "Noise_IK_25519_AESGCM_SHA256"
#define MAX_PLAIN 4096u
struct falafel_e2ee {
    NoiseHandshakeState *handshake;
    NoiseCipherState *send, *receive;
    falafel_e2ee_state state;
    falafel_pairing_role role;
    unsigned flight;
    uint64_t send_seq, recv_seq;
    uint8_t peer[32];
};
void falafel_e2ee_close(falafel_e2ee *s) { if (s) s->state = FALAFEL_E2EE_CLOSED; }
void falafel_e2ee_free(falafel_e2ee *s)
{
    if (!s) return;
    noise_cipherstate_free(s->send); noise_cipherstate_free(s->receive);
    noise_handshakestate_free(s->handshake);
    memset(s, 0, sizeof(*s)); free(s);
}
falafel_e2ee_state falafel_e2ee_status(const falafel_e2ee *s) { return s ? s->state : FALAFEL_E2EE_CLOSED; }
int falafel_e2ee_new(falafel_e2ee **out, falafel_pairing_role role, const falafel_pairing_persistent_state *identity)
{
    falafel_e2ee *s; NoiseDHState *dh;
    if (!out || !identity || falafel_pairing_persistent_validate(identity) || !identity->has_pairing ||
        (role != FALAFEL_PAIRING_INITIATOR && role != FALAFEL_PAIRING_RESPONDER)) return -1;
    *out = NULL; s = calloc(1, sizeof(*s)); if (!s) return -1;
    s->role = role; memcpy(s->peer, identity->peer_public, 32);
    if (noise_handshakestate_new_by_name(&s->handshake, SUITE,
            role == FALAFEL_PAIRING_INITIATOR ? NOISE_ROLE_INITIATOR : NOISE_ROLE_RESPONDER)) goto error;
    dh = noise_handshakestate_get_local_keypair_dh(s->handshake);
    if (!dh || noise_dhstate_set_keypair_private(dh, identity->local_private, 32)) goto error;
    if (role == FALAFEL_PAIRING_INITIATOR) {
        dh = noise_handshakestate_get_remote_public_key_dh(s->handshake);
        if (!dh || noise_dhstate_set_public_key(dh, s->peer, 32)) goto error;
    }
    if (noise_handshakestate_start(s->handshake)) goto error;
    s->state = FALAFEL_E2EE_HANDSHAKE; *out = s; return 0;
error: falafel_e2ee_free(s); return -1;
}
static int finish(falafel_e2ee *s)
{
    uint8_t peer[32]; NoiseDHState *dh = noise_handshakestate_get_remote_public_key_dh(s->handshake);
    if (!dh || noise_dhstate_get_public_key(dh, peer, sizeof(peer)) || memcmp(peer, s->peer, 32) ||
        noise_handshakestate_get_action(s->handshake) != NOISE_ACTION_SPLIT ||
        noise_handshakestate_split(s->handshake, &s->send, &s->receive)) {
        falafel_e2ee_close(s); return -1;
    }
    s->state = FALAFEL_E2EE_READY; return 0;
}
int falafel_e2ee_write_handshake(falafel_e2ee *s, uint8_t *out, size_t capacity, size_t *size)
{
    NoiseBuffer msg, payload; uint8_t empty = 0;
    if (!s || !out || !size || s->state != FALAFEL_E2EE_HANDSHAKE || s->flight > 1 ||
        (s->flight == 0) != (s->role == FALAFEL_PAIRING_INITIATOR) || capacity > 65535) goto error;
    noise_buffer_set_output(msg, out, capacity); noise_buffer_set_input(payload, &empty, 0);
    if (noise_handshakestate_write_message(s->handshake, &msg, &payload)) goto error;
    *size = msg.size; ++s->flight; return s->flight == 2 ? finish(s) : 0;
error: falafel_e2ee_close(s); return -1;
}
int falafel_e2ee_read_handshake(falafel_e2ee *s, const uint8_t *data, size_t size)
{
    NoiseBuffer msg, payload; uint8_t empty;
    if (!s || !data || s->state != FALAFEL_E2EE_HANDSHAKE || s->flight > 1 || size > 256 ||
        (s->flight == 0) == (s->role == FALAFEL_PAIRING_INITIATOR)) goto error;
    noise_buffer_set_input(msg, (uint8_t *)data, size); noise_buffer_set_output(payload, &empty, 1);
    if (noise_handshakestate_read_message(s->handshake, &msg, &payload) || payload.size) goto error;
    ++s->flight;
    /* Check the initiator identity before the responder sends flight two. */
    if (s->role == FALAFEL_PAIRING_RESPONDER) {
        uint8_t peer[32]; NoiseDHState *dh = noise_handshakestate_get_remote_public_key_dh(s->handshake);
        if (!dh || noise_dhstate_get_public_key(dh, peer, 32) || memcmp(peer, s->peer, 32)) goto error;
    }
    return s->flight == 2 ? finish(s) : 0;
error: falafel_e2ee_close(s); return -1;
}
static void sequence(uint8_t out[8], uint64_t n) { for (unsigned i = 0; i < 8; ++i) out[i] = (uint8_t)(n >> (56 - 8 * i)); }
int falafel_e2ee_encrypt(falafel_e2ee *s, const uint8_t *plain, size_t size, uint8_t *out, size_t capacity, size_t *out_size)
{
    NoiseBuffer b;
    if (!s || s->state != FALAFEL_E2EE_READY || !plain || !out || !out_size || size > MAX_PLAIN ||
        capacity < size + 24 || s->send_seq == UINT64_MAX) goto error;
    sequence(out, s->send_seq); memcpy(out + 8, plain, size);
    noise_buffer_set_inout(b, out + 8, size, capacity - 8);
    if (noise_cipherstate_encrypt_with_ad(s->send, out, 8, &b)) goto error;
    ++s->send_seq; *out_size = b.size + 8; return 0;
error: falafel_e2ee_close(s); return -1;
}
int falafel_e2ee_decrypt(falafel_e2ee *s, const uint8_t *wire, size_t size, uint8_t *out, size_t capacity, size_t *out_size)
{
    NoiseBuffer b; uint8_t expected[8];
    if (!s || s->state != FALAFEL_E2EE_READY || !wire || !out || !out_size || size < 24 ||
        size > MAX_PLAIN + 24 || capacity < size - 8 || s->recv_seq == UINT64_MAX) goto error;
    sequence(expected, s->recv_seq); if (memcmp(expected, wire, 8)) goto error;
    memcpy(out, wire + 8, size - 8); noise_buffer_set_inout(b, out, size - 8, capacity);
    if (noise_cipherstate_decrypt_with_ad(s->receive, wire, 8, &b)) { memset(out, 0, size - 8); goto error; }
    ++s->recv_seq; *out_size = b.size; return 0;
error: falafel_e2ee_close(s); return -1;
}
