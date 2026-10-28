#include <noise/protocol.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_timer.h"
#else
#include <time.h>
#endif

#define PROTOCOL "Noise_XX_25519_AESGCM_SHA256"
#define MESSAGE_CAPACITY 256u

typedef struct {
    NoiseHandshakeState *initiator;
    NoiseHandshakeState *responder;
    NoiseCipherState *i_send;
    NoiseCipherState *i_recv;
    NoiseCipherState *r_send;
    NoiseCipherState *r_recv;
    uint8_t messages[3][MESSAGE_CAPACITY];
    size_t lengths[3];
    uint8_t hash[32];
} Session;

static void cleanup(Session *s)
{
    noise_cipherstate_free(s->i_send); noise_cipherstate_free(s->i_recv);
    noise_cipherstate_free(s->r_send); noise_cipherstate_free(s->r_recv);
    noise_handshakestate_free(s->initiator); noise_handshakestate_free(s->responder);
    memset(s, 0, sizeof(*s));
}

static int setup(Session *s)
{
    NoiseDHState *dh;
    memset(s, 0, sizeof(*s));
    if (noise_handshakestate_new_by_name(&s->initiator, PROTOCOL, NOISE_ROLE_INITIATOR) ||
        noise_handshakestate_new_by_name(&s->responder, PROTOCOL, NOISE_ROLE_RESPONDER)) return -1;
    dh = noise_handshakestate_get_local_keypair_dh(s->initiator);
    if (noise_dhstate_generate_keypair(dh)) return -1;
    dh = noise_handshakestate_get_local_keypair_dh(s->responder);
    if (noise_dhstate_generate_keypair(dh)) return -1;
    if (noise_handshakestate_start(s->initiator) || noise_handshakestate_start(s->responder)) return -1;
    return 0;
}

static int transfer(NoiseHandshakeState *send, NoiseHandshakeState *receive,
                    uint8_t *wire, size_t *wire_len)
{
    static const uint8_t payload[] = "falafel-noise-gate";
    uint8_t recovered[sizeof(payload)], received[MESSAGE_CAPACITY];
    NoiseBuffer message, input, output;
    noise_buffer_set_output(message, wire, MESSAGE_CAPACITY);
    noise_buffer_set_input(input, (uint8_t *)payload, sizeof(payload));
    if (noise_handshakestate_write_message(send, &message, &input)) return -1;
    *wire_len = message.size;
    memcpy(received, wire, message.size);
    noise_buffer_set_input(message, received, message.size);
    noise_buffer_set_output(output, recovered, sizeof(recovered));
    if (noise_handshakestate_read_message(receive, &message, &output)) return -1;
    return output.size == sizeof(payload) && memcmp(payload, recovered, sizeof(payload)) == 0 ? 0 : -1;
}

static int complete(Session *s)
{
    uint8_t peer_hash[32];
    if (setup(s) || transfer(s->initiator, s->responder, s->messages[0], &s->lengths[0]) ||
        transfer(s->responder, s->initiator, s->messages[1], &s->lengths[1]) ||
        transfer(s->initiator, s->responder, s->messages[2], &s->lengths[2])) return -1;
    if (noise_handshakestate_get_action(s->initiator) != NOISE_ACTION_SPLIT ||
        noise_handshakestate_get_action(s->responder) != NOISE_ACTION_SPLIT) return -1;
    if (noise_handshakestate_get_handshake_hash(s->initiator, s->hash, sizeof(s->hash)) ||
        noise_handshakestate_get_handshake_hash(s->responder, peer_hash, sizeof(peer_hash)) ||
        memcmp(s->hash, peer_hash, sizeof(peer_hash))) return -1;
    if (noise_handshakestate_split(s->initiator, &s->i_send, &s->i_recv) ||
        noise_handshakestate_split(s->responder, &s->r_send, &s->r_recv)) return -1;
    return 0;
}

static int transport(NoiseCipherState *send, NoiseCipherState *receive, int tamper)
{
    static const uint8_t clear[] = "transport proof";
    uint8_t data[64]; NoiseBuffer buffer;
    memcpy(data, clear, sizeof(clear));
    noise_buffer_set_inout(buffer, data, sizeof(clear), sizeof(data));
    if (noise_cipherstate_encrypt(send, &buffer)) return -1;
    if (tamper) data[buffer.size - 1u] ^= 1u;
    if (noise_cipherstate_decrypt(receive, &buffer) != (tamper ? NOISE_ERROR_MAC_FAILURE : NOISE_ERROR_NONE)) return -1;
    return tamper || (buffer.size == sizeof(clear) && memcmp(data, clear, sizeof(clear)) == 0) ? 0 : -1;
}

static int rejected_variant(int step, int mode)
{
    Session s; NoiseHandshakeState *send, *recv; NoiseBuffer msg, payload; uint8_t out[32]; int rc = 0;
    if (setup(&s)) return -1;
    for (int index = 0; index <= step; ++index) {
        send = (index == 1) ? s.responder : s.initiator; recv = (index == 1) ? s.initiator : s.responder;
        noise_buffer_set_output(msg, s.messages[index], MESSAGE_CAPACITY);
        noise_buffer_set_input(payload, out, 0);
        if (noise_handshakestate_write_message(send, &msg, &payload)) { rc = -1; break; }
        if (index == step) {
            if (mode == 1 && msg.size) --msg.size;
            if (mode == 2 && msg.size) s.messages[index][msg.size - 1u] ^= 1u;
            if (mode == 3) msg.size = MESSAGE_CAPACITY;
            if (mode == 4) memset(s.messages[index], 0, msg.size);
        }
        noise_buffer_set_output(payload, out, sizeof(out));
        rc = noise_handshakestate_read_message(recv, &msg, &payload);
        if (index < step && rc) break;
    }
    cleanup(&s);
    return rc == NOISE_ERROR_NONE ? -1 : 0;
}

static int altered_message1_eventually_rejected(void)
{
    Session s; NoiseBuffer msg, payload; uint8_t out[64]; int rc;
    if (setup(&s)) return -1;
    noise_buffer_set_output(msg, s.messages[0], MESSAGE_CAPACITY);
    noise_buffer_set_input(payload, out, 0);
    if (noise_handshakestate_write_message(s.initiator, &msg, &payload)) return -1;
    s.messages[0][0] ^= 1u; /* Message 1 is unauthenticated in XX. */
    noise_buffer_set_output(payload, out, sizeof(out));
    if (noise_handshakestate_read_message(s.responder, &msg, &payload)) return -1;
    noise_buffer_set_output(msg, s.messages[1], MESSAGE_CAPACITY);
    noise_buffer_set_input(payload, out, 0);
    if (noise_handshakestate_write_message(s.responder, &msg, &payload)) return -1;
    noise_buffer_set_output(payload, out, sizeof(out));
    rc = noise_handshakestate_read_message(s.initiator, &msg, &payload);
    cleanup(&s);
    return rc == NOISE_ERROR_MAC_FAILURE ? 0 : -1;
}

static int oversized_message_rejected(void)
{
    Session s; NoiseBuffer msg, payload; uint8_t out[1];
    uint8_t *wire = malloc(65536u); int rc;
    if (!wire || setup(&s)) { free(wire); return -1; }
    memset(wire, 0, 65536u);
    noise_buffer_set_input(msg, wire, 65536u);
    noise_buffer_set_output(payload, out, sizeof(out));
    rc = noise_handshakestate_read_message(s.responder, &msg, &payload);
    cleanup(&s); free(wire);
    return rc != NOISE_ERROR_NONE ? 0 : -1;
}

static uint64_t now_ns(void)
{
#ifdef ESP_PLATFORM
    return (uint64_t)esp_timer_get_time() * 1000u;
#else
    struct timespec ts; timespec_get(&ts, TIME_UTC);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}

int noise_feasibility_run(void)
{
    Session first, second; uint64_t start, elapsed; const unsigned rounds = 100;
    assert(complete(&first) == 0);
    assert(transport(first.i_send, first.r_recv, 0) == 0);
    assert(transport(first.r_send, first.i_recv, 0) == 0);
    assert(transport(first.i_send, first.r_recv, 1) == 0);
    { uint8_t data[1]; NoiseBuffer b, p; noise_buffer_set_output(b, data, sizeof(data));
      noise_buffer_set_input(p, data, 0);
      assert(noise_handshakestate_write_message(first.initiator, &b, &p) == NOISE_ERROR_INVALID_STATE); }
    assert(complete(&second) == 0);
    assert(memcmp(first.messages[0], second.messages[0], first.lengths[0]) != 0);
    cleanup(&first); cleanup(&second);

    assert(rejected_variant(0, 1) == 0); /* truncated message 1 */
    assert(rejected_variant(1, 2) == 0); /* altered message 2 */
    assert(rejected_variant(2, 2) == 0); /* altered message 3 */
    assert(rejected_variant(0, 3) == 0); /* oversized message */
    assert(rejected_variant(0, 4) == 0); /* low-order/all-zero ephemeral */
    assert(rejected_variant(1, 1) == 0); assert(rejected_variant(2, 1) == 0);
    assert(altered_message1_eventually_rejected() == 0);
    assert(oversized_message_rejected() == 0);

    assert(setup(&first) == 0);
    { uint8_t data[32]; NoiseBuffer b, p; noise_buffer_set_output(b, data, sizeof(data));
      noise_buffer_set_input(p, data, 0);
      assert(noise_handshakestate_write_message(first.responder, &b, &p) == NOISE_ERROR_INVALID_STATE); }
    cleanup(&first);

    assert(complete(&first) == 0);
    { NoiseCipherState *wrong = NULL; uint8_t data[64] = "wrong key"; NoiseBuffer b;
      assert(noise_cipherstate_new_by_name(&wrong, "AESGCM") == NOISE_ERROR_NONE);
      { uint8_t key[32] = {0}; assert(noise_cipherstate_init_key(wrong, key, sizeof(key)) == NOISE_ERROR_NONE); }
      noise_buffer_set_inout(b, data, sizeof("wrong key"), sizeof(data));
      assert(noise_cipherstate_encrypt(first.i_send, &b) == NOISE_ERROR_NONE);
      assert(noise_cipherstate_decrypt(wrong, &b) == NOISE_ERROR_MAC_FAILURE);
      noise_cipherstate_free(wrong); }
    cleanup(&first);

    start = now_ns();
    for (unsigned i = 0; i < rounds; ++i) { assert(complete(&first) == 0); cleanup(&first); }
    elapsed = now_ns() - start;
    printf("Noise-C revision: cfe25410979a87391bb9ac8d4d4bef64e9f268c6\n");
    printf("suite: %s\n", PROTOCOL);
    printf("tests: PASS\nhandshakes: %u\naverage_handshake_us: %.2f\n", rounds,
           (double)elapsed / (double)rounds / 1000.0);
    return 0;
}

#ifndef ESP_PLATFORM
int main(void) { return noise_feasibility_run(); }
#endif
