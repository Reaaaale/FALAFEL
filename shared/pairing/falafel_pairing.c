#include "falafel_pairing.h"

#include <noise/protocol.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAIRING_PROTOCOL "Noise_XX_25519_AESGCM_SHA256"
#define CONFIRM_PLAIN_SIZE 54u
#define CONFIRM_WIRE_SIZE (CONFIRM_PLAIN_SIZE + 16u)

struct falafel_pairing {
    NoiseHandshakeState *handshake;
    NoiseCipherState *send;
    NoiseCipherState *receive;
    falafel_pairing_state state;
    falafel_pairing_role role;
    unsigned flight;
    int local_confirmed;
    int remote_confirmed;
    char sas[FALAFEL_PAIRING_SAS_LENGTH + 1u];
    uint8_t transcript[32];
};

static void fail(falafel_pairing *p)
{
    if (p && p->state != FALAFEL_PAIRING_REJECTED)
        p->state = FALAFEL_PAIRING_ERROR;
}

static int sha256(const uint8_t *data, size_t size, uint8_t output[32])
{
    NoiseHashState *hash = NULL;
    int rc = noise_hashstate_new_by_name(&hash, "SHA256");
    if (!rc)
        rc = noise_hashstate_hash_one(hash, data, size, output, 32);
    noise_hashstate_free(hash);
    return rc;
}

static int derive_sas(falafel_pairing *p)
{
    static const uint8_t domain[] = "falafel/pairing/sas/v1";
    uint8_t input[sizeof(domain) - 1u + 32u + 4u], digest[32];
    uint32_t counter;
    memcpy(input, domain, sizeof(domain) - 1u);
    memcpy(input + sizeof(domain) - 1u, p->transcript, 32u);
    for (counter = 0; counter != UINT32_MAX; ++counter) {
        uint64_t value;
        size_t offset = sizeof(input) - 4u;
        input[offset] = (uint8_t)(counter >> 24);
        input[offset + 1u] = (uint8_t)(counter >> 16);
        input[offset + 2u] = (uint8_t)(counter >> 8);
        input[offset + 3u] = (uint8_t)counter;
        if (sha256(input, sizeof(input), digest))
            return -1;
        value = ((uint64_t)digest[0] << 32) | ((uint64_t)digest[1] << 24) |
                ((uint64_t)digest[2] << 16) | ((uint64_t)digest[3] << 8) | digest[4];
        if (value < UINT64_C(1000000000000)) {
            (void)snprintf(p->sas, sizeof(p->sas), "%012llu",
                           (unsigned long long)value);
            memset(digest, 0, sizeof(digest));
            memset(input, 0, sizeof(input));
            return 0;
        }
    }
    return -1;
}

static int finish_handshake(falafel_pairing *p)
{
    if (noise_handshakestate_get_action(p->handshake) != NOISE_ACTION_SPLIT ||
        noise_handshakestate_get_handshake_hash(p->handshake, p->transcript,
                                                sizeof(p->transcript)) ||
        noise_handshakestate_split(p->handshake, &p->send, &p->receive) || derive_sas(p)) {
        fail(p);
        return -1;
    }
    p->state = FALAFEL_PAIRING_WAIT_SAS;
    return 0;
}

int falafel_pairing_generate_identity(uint8_t private_key[32])
{
    NoiseDHState *dh = NULL;
    uint8_t public_key[32];
    int rc;
    if (!private_key || noise_dhstate_new_by_name(&dh, "25519"))
        return -1;
    rc = noise_dhstate_generate_keypair(dh);
    if (!rc)
        rc = noise_dhstate_get_keypair(dh, private_key, 32, public_key, 32);
    memset(public_key, 0, sizeof(public_key));
    noise_dhstate_free(dh);
    return rc ? -1 : 0;
}

int falafel_pairing_identity_public(const uint8_t private_key[32], uint8_t public_key[32])
{
    NoiseDHState *dh = NULL;
    int rc;
    if (!private_key || !public_key || noise_dhstate_new_by_name(&dh, "25519"))
        return -1;
    rc = noise_dhstate_set_keypair_private(dh, private_key, 32);
    if (!rc)
        rc = noise_dhstate_get_public_key(dh, public_key, 32);
    noise_dhstate_free(dh);
    return rc ? -1 : 0;
}

int falafel_pairing_new(falafel_pairing **result, falafel_pairing_role role,
                        const uint8_t private_key[32])
{
    falafel_pairing *p;
    NoiseDHState *dh;
    int noise_role;
    if (!result || !private_key ||
        (role != FALAFEL_PAIRING_INITIATOR && role != FALAFEL_PAIRING_RESPONDER))
        return -1;
    *result = NULL;
    p = (falafel_pairing *)calloc(1, sizeof(*p));
    if (!p)
        return -1;
    p->state = FALAFEL_PAIRING_INITIAL;
    p->role = role;
    noise_role = role == FALAFEL_PAIRING_INITIATOR ? NOISE_ROLE_INITIATOR : NOISE_ROLE_RESPONDER;
    if (noise_handshakestate_new_by_name(&p->handshake, PAIRING_PROTOCOL, noise_role))
        goto error;
    dh = noise_handshakestate_get_local_keypair_dh(p->handshake);
    if (!dh || noise_dhstate_set_keypair_private(dh, private_key, 32) ||
        noise_handshakestate_start(p->handshake))
        goto error;
    *result = p;
    return 0;
error:
    falafel_pairing_free(p);
    return -1;
}

void falafel_pairing_free(falafel_pairing *p)
{
    if (!p) return;
    noise_cipherstate_free(p->send);
    noise_cipherstate_free(p->receive);
    noise_handshakestate_free(p->handshake);
    memset(p, 0, sizeof(*p));
    free(p);
}

falafel_pairing_state falafel_pairing_get_state(const falafel_pairing *p)
{
    return p ? p->state : FALAFEL_PAIRING_ERROR;
}

static int expected_sender(const falafel_pairing *p)
{
    return (p->flight == 0u || p->flight == 2u) ? FALAFEL_PAIRING_INITIATOR
                                                : FALAFEL_PAIRING_RESPONDER;
}

int falafel_pairing_write_handshake(falafel_pairing *p, uint8_t *message,
                                    size_t capacity, size_t *message_size)
{
    NoiseBuffer wire, payload;
    uint8_t empty = 0;
    if (p && p->state == FALAFEL_PAIRING_INITIAL)
        p->state = FALAFEL_PAIRING_HANDSHAKE;
    if (!p || !message || !message_size || p->state != FALAFEL_PAIRING_HANDSHAKE ||
        p->flight >= 3u || (int)p->role != expected_sender(p) ||
        capacity > UINT16_MAX) {
        fail(p); return -1;
    }
    noise_buffer_set_output(wire, message, capacity);
    noise_buffer_set_input(payload, &empty, 0);
    if (noise_handshakestate_write_message(p->handshake, &wire, &payload)) {
        fail(p); return -1;
    }
    *message_size = wire.size;
    ++p->flight;
    if (p->flight == 3u)
        return finish_handshake(p);
    return 0;
}

int falafel_pairing_read_handshake(falafel_pairing *p, const uint8_t *message,
                                   size_t message_size)
{
    NoiseBuffer wire, payload;
    uint8_t empty;
    if (p && p->state == FALAFEL_PAIRING_INITIAL)
        p->state = FALAFEL_PAIRING_HANDSHAKE;
    if (!p || !message || p->state != FALAFEL_PAIRING_HANDSHAKE ||
        p->flight >= 3u || (int)p->role == expected_sender(p) ||
        message_size > FALAFEL_PAIRING_MAX_MESSAGE) {
        fail(p); return -1;
    }
    noise_buffer_set_input(wire, (uint8_t *)message, message_size);
    noise_buffer_set_output(payload, &empty, sizeof(empty));
    if (noise_handshakestate_read_message(p->handshake, &wire, &payload) || payload.size) {
        fail(p); return -1;
    }
    ++p->flight;
    if (p->flight == 3u)
        return finish_handshake(p);
    return 0;
}

int falafel_pairing_get_sas(const falafel_pairing *p, char sas[13])
{
    if (!p || !sas || (p->state != FALAFEL_PAIRING_WAIT_SAS &&
                       p->state != FALAFEL_PAIRING_LOCAL_CONFIRMED &&
                       p->state != FALAFEL_PAIRING_COMPLETE))
        return -1;
    memcpy(sas, p->sas, sizeof(p->sas));
    return 0;
}

static int write_decision(falafel_pairing *p, int accepted, uint8_t *message,
                          size_t capacity, size_t *message_size)
{
    uint8_t clear[CONFIRM_PLAIN_SIZE] = {0};
    NoiseBuffer buffer;
    if (!p || !message || !message_size || p->state != FALAFEL_PAIRING_WAIT_SAS ||
        capacity < CONFIRM_WIRE_SIZE) {
        fail(p); return -1;
    }
    memcpy(clear, "FALPAIR1", 8);
    clear[8] = (uint8_t)(accepted ? 1 : 0);
    clear[9] = (uint8_t)p->role;
    memcpy(clear + 10, p->sas, FALAFEL_PAIRING_SAS_LENGTH);
    memcpy(clear + 22, p->transcript, 32);
    memcpy(message, clear, sizeof(clear));
    noise_buffer_set_inout(buffer, message, sizeof(clear), capacity);
    if (noise_cipherstate_encrypt(p->send, &buffer)) {
        memset(clear, 0, sizeof(clear)); fail(p); return -1;
    }
    memset(clear, 0, sizeof(clear));
    *message_size = buffer.size;
    if (accepted) {
        p->local_confirmed = 1;
        p->state = p->remote_confirmed ? FALAFEL_PAIRING_COMPLETE
                                       : FALAFEL_PAIRING_LOCAL_CONFIRMED;
    } else {
        p->state = FALAFEL_PAIRING_REJECTED;
    }
    return 0;
}

int falafel_pairing_confirm(falafel_pairing *p, const char *expected_sas,
                            uint8_t *message, size_t capacity, size_t *message_size)
{
    if (!p || !expected_sas || strlen(expected_sas) != FALAFEL_PAIRING_SAS_LENGTH ||
        memcmp(expected_sas, p->sas, FALAFEL_PAIRING_SAS_LENGTH) != 0)
        return falafel_pairing_reject(p, message, capacity, message_size);
    return write_decision(p, 1, message, capacity, message_size);
}

int falafel_pairing_reject(falafel_pairing *p, uint8_t *message,
                           size_t capacity, size_t *message_size)
{
    return write_decision(p, 0, message, capacity, message_size);
}

int falafel_pairing_read_confirmation(falafel_pairing *p,
                                      const uint8_t *message, size_t message_size)
{
    uint8_t clear[CONFIRM_WIRE_SIZE];
    NoiseBuffer buffer;
    int peer_role;
    if (!p || !message ||
        (p->state != FALAFEL_PAIRING_WAIT_SAS &&
         p->state != FALAFEL_PAIRING_LOCAL_CONFIRMED) || p->remote_confirmed ||
        message_size != CONFIRM_WIRE_SIZE) {
        fail(p); return -1;
    }
    memcpy(clear, message, message_size);
    noise_buffer_set_inout(buffer, clear, message_size, sizeof(clear));
    if (noise_cipherstate_decrypt(p->receive, &buffer) || buffer.size != CONFIRM_PLAIN_SIZE) {
        memset(clear, 0, sizeof(clear)); fail(p); return -1;
    }
    peer_role = p->role == FALAFEL_PAIRING_INITIATOR ? FALAFEL_PAIRING_RESPONDER
                                                     : FALAFEL_PAIRING_INITIATOR;
    if (memcmp(clear, "FALPAIR1", 8) || clear[9] != peer_role ||
        memcmp(clear + 10, p->sas, FALAFEL_PAIRING_SAS_LENGTH) ||
        memcmp(clear + 22, p->transcript, 32) || clear[8] > 1u) {
        memset(clear, 0, sizeof(clear)); fail(p); return -1;
    }
    if (!clear[8]) {
        memset(clear, 0, sizeof(clear)); p->state = FALAFEL_PAIRING_REJECTED; return 0;
    }
    memset(clear, 0, sizeof(clear));
    p->remote_confirmed = 1;
    p->state = p->local_confirmed ? FALAFEL_PAIRING_COMPLETE : FALAFEL_PAIRING_WAIT_SAS;
    return 0;
}

int falafel_pairing_get_peer_public_key(const falafel_pairing *p, uint8_t public_key[32])
{
    NoiseDHState *dh;
    if (!p || !public_key || p->state != FALAFEL_PAIRING_COMPLETE)
        return -1;
    dh = noise_handshakestate_get_remote_public_key_dh(p->handshake);
    return !dh || noise_dhstate_get_public_key(dh, public_key, 32) ? -1 : 0;
}
