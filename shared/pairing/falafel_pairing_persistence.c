#include "falafel_pairing_persistence.h"
#include <string.h>

static int all_zero(const uint8_t *data, size_t size)
{
    uint8_t value = 0;
    while (size--) value |= *data++;
    return value == 0;
}

static uint32_t crc32(const uint8_t *data, size_t size)
{
    uint32_t crc = UINT32_C(0xffffffff);
    while (size--) {
        crc ^= *data++;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & (uint32_t)-(int32_t)(crc & 1u));
    }
    return ~crc;
}

static void put64(uint8_t *p, uint64_t value)
{
    for (unsigned i = 0; i < 8; ++i) p[i] = (uint8_t)(value >> (56u - i * 8u));
}
static uint64_t get64(const uint8_t *p)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value = (value << 8) | p[i];
    return value;
}
static void put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24); p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8); p[3] = (uint8_t)value;
}
static uint32_t get32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

int falafel_pairing_persistent_init(falafel_pairing_persistent_state *state,
                                    const uint8_t local_private[32])
{
    if (!state || !local_private) return -1;
    memset(state, 0, sizeof(*state));
    memcpy(state->local_private, local_private, 32);
    if (falafel_pairing_identity_public(local_private, state->local_public) ||
        all_zero(state->local_public, 32)) {
        falafel_pairing_persistent_clear(state); return -1;
    }
    state->generation = 1;
    return 0;
}

int falafel_pairing_persistent_validate(const falafel_pairing_persistent_state *state)
{
    uint8_t public_key[32]; int ok;
    if (!state || state->generation == 0 || (state->has_pairing != 0 && state->has_pairing != 1) ||
        all_zero(state->local_private, 32) || all_zero(state->local_public, 32) ||
        (state->has_pairing ? all_zero(state->peer_public, 32) : !all_zero(state->peer_public, 32)))
        return -1;
    ok = !falafel_pairing_identity_public(state->local_private, public_key) &&
         memcmp(public_key, state->local_public, 32) == 0;
    memset(public_key, 0, sizeof(public_key));
    return ok ? 0 : -1;
}

int falafel_pairing_persistent_complete(falafel_pairing_persistent_state *state,
                                        const falafel_pairing *pairing)
{
    uint8_t peer[32];
    if (falafel_pairing_persistent_validate(state) || state->has_pairing ||
        falafel_pairing_get_state(pairing) != FALAFEL_PAIRING_COMPLETE ||
        falafel_pairing_get_peer_public_key(pairing, peer) || all_zero(peer, 32))
        return -1;
    memcpy(state->peer_public, peer, 32); memset(peer, 0, sizeof(peer));
    state->has_pairing = 1; ++state->generation;
    return 0;
}

int falafel_pairing_persistent_encode(const falafel_pairing_persistent_state *state,
                                      uint8_t output[88])
{
    if (!output || falafel_pairing_persistent_validate(state)) return -1;
    memset(output, 0, 88); memcpy(output, "FALPST1", 7); output[8] = 1;
    output[9] = (uint8_t)state->has_pairing; put64(output + 12, state->generation);
    memcpy(output + 20, state->local_public, 32); memcpy(output + 52, state->peer_public, 32);
    put32(output + 84, crc32(output, 84)); return 0;
}

int falafel_pairing_persistent_decode(falafel_pairing_persistent_state *state,
                                      const uint8_t local_private[32],
                                      const uint8_t *input, size_t size)
{
    falafel_pairing_persistent_state decoded;
    if (!state || !local_private || !input || size != 88 || memcmp(input, "FALPST1", 7) ||
        input[7] || input[8] != 1 || input[9] > 1 || input[10] || input[11] ||
        get32(input + 84) != crc32(input, 84)) return -1;
    memset(&decoded, 0, sizeof(decoded)); memcpy(decoded.local_private, local_private, 32);
    decoded.has_pairing = input[9]; decoded.generation = get64(input + 12);
    memcpy(decoded.local_public, input + 20, 32); memcpy(decoded.peer_public, input + 52, 32);
    if (falafel_pairing_persistent_validate(&decoded)) {
        falafel_pairing_persistent_clear(&decoded); return -1;
    }
    *state = decoded; return 0;
}

void falafel_pairing_persistent_clear(falafel_pairing_persistent_state *state)
{
    if (state) { volatile uint8_t *p = (volatile uint8_t *)state; size_t n = sizeof(*state); while (n--) *p++ = 0; }
}
