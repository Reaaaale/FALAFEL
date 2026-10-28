#ifndef FALAFEL_PAIRING_PERSISTENCE_H
#define FALAFEL_PAIRING_PERSISTENCE_H

#include "falafel_pairing.h"
#include <stddef.h>
#include <stdint.h>

#define FALAFEL_PAIRING_METADATA_SIZE 88u

typedef struct {
    uint8_t local_private[32];
    uint8_t local_public[32];
    uint8_t peer_public[32];
    uint64_t generation;
    int has_pairing;
} falafel_pairing_persistent_state;

int falafel_pairing_persistent_init(falafel_pairing_persistent_state *state,
                                    const uint8_t local_private[32]);
int falafel_pairing_persistent_validate(const falafel_pairing_persistent_state *state);
int falafel_pairing_persistent_complete(falafel_pairing_persistent_state *state,
                                        const falafel_pairing *completed_pairing);
int falafel_pairing_persistent_encode(const falafel_pairing_persistent_state *state,
                                      uint8_t output[FALAFEL_PAIRING_METADATA_SIZE]);
int falafel_pairing_persistent_decode(falafel_pairing_persistent_state *state,
                                      const uint8_t local_private[32],
                                      const uint8_t *input, size_t input_size);
void falafel_pairing_persistent_clear(falafel_pairing_persistent_state *state);

#endif
