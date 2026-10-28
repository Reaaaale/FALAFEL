#include "falafel_pairing_persistence.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    falafel_pairing_persistent_state state, decoded;
    uint8_t key[32], other[32], encoded[88], corrupted[88];
    assert(!falafel_pairing_generate_identity(key));
    assert(!falafel_pairing_generate_identity(other));
    assert(!falafel_pairing_persistent_init(&state, key));
    assert(!state.has_pairing && state.generation == 1);
    assert(!falafel_pairing_persistent_validate(&state));
    assert(!falafel_pairing_persistent_encode(&state, encoded));
    assert(!falafel_pairing_persistent_decode(&decoded, key, encoded, sizeof(encoded)));
    assert(!memcmp(decoded.local_private, key, 32));
    assert(!decoded.has_pairing);
    memcpy(corrupted, encoded, sizeof(corrupted)); corrupted[25] ^= 1;
    assert(falafel_pairing_persistent_decode(&decoded, key, corrupted, sizeof(corrupted)));
    assert(falafel_pairing_persistent_decode(&decoded, other, encoded, sizeof(encoded)));
    assert(falafel_pairing_persistent_decode(&decoded, key, encoded, sizeof(encoded) - 1));
    state.has_pairing = 1;
    assert(falafel_pairing_persistent_validate(&state));
    memset(state.peer_public, 1, sizeof(state.peer_public));
    assert(!falafel_pairing_persistent_validate(&state));
    state.has_pairing = 0;
    assert(falafel_pairing_persistent_validate(&state));
    falafel_pairing_persistent_clear(&state); falafel_pairing_persistent_clear(&decoded);
    memset(key, 0, sizeof(key)); memset(other, 0, sizeof(other));
    puts("pairing persistence codec tests: PASS"); return 0;
}
