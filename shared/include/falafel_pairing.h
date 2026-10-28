#ifndef FALAFEL_PAIRING_H
#define FALAFEL_PAIRING_H

#include <stddef.h>
#include <stdint.h>

#define FALAFEL_PAIRING_IDENTITY_SIZE 32u
#define FALAFEL_PAIRING_SAS_LENGTH 12u
#define FALAFEL_PAIRING_MAX_MESSAGE 256u

typedef enum {
    FALAFEL_PAIRING_INITIAL = 0,
    FALAFEL_PAIRING_HANDSHAKE,
    FALAFEL_PAIRING_WAIT_SAS,
    FALAFEL_PAIRING_LOCAL_CONFIRMED,
    FALAFEL_PAIRING_COMPLETE,
    FALAFEL_PAIRING_REJECTED,
    FALAFEL_PAIRING_ERROR
} falafel_pairing_state;

typedef enum {
    FALAFEL_PAIRING_INITIATOR = 1,
    FALAFEL_PAIRING_RESPONDER = 2
} falafel_pairing_role;

typedef struct falafel_pairing falafel_pairing;

int falafel_pairing_generate_identity(uint8_t private_key[FALAFEL_PAIRING_IDENTITY_SIZE]);
int falafel_pairing_identity_public(const uint8_t private_key[FALAFEL_PAIRING_IDENTITY_SIZE],
                                    uint8_t public_key[FALAFEL_PAIRING_IDENTITY_SIZE]);
int falafel_pairing_new(falafel_pairing **pairing, falafel_pairing_role role,
                        const uint8_t private_key[FALAFEL_PAIRING_IDENTITY_SIZE]);
void falafel_pairing_free(falafel_pairing *pairing);
falafel_pairing_state falafel_pairing_get_state(const falafel_pairing *pairing);

/* Writes the next XX flight. Only valid when this role is due to send. */
int falafel_pairing_write_handshake(falafel_pairing *pairing, uint8_t *message,
                                    size_t capacity, size_t *message_size);
/* Consumes the next XX flight. Only valid when this role is due to receive. */
int falafel_pairing_read_handshake(falafel_pairing *pairing, const uint8_t *message,
                                   size_t message_size);
int falafel_pairing_get_sas(const falafel_pairing *pairing,
                            char sas[FALAFEL_PAIRING_SAS_LENGTH + 1u]);

/* expected_sas must be the 12 digits displayed to the local user. */
int falafel_pairing_confirm(falafel_pairing *pairing, const char *expected_sas,
                            uint8_t *message, size_t capacity, size_t *message_size);
int falafel_pairing_reject(falafel_pairing *pairing, uint8_t *message,
                           size_t capacity, size_t *message_size);
int falafel_pairing_read_confirmation(falafel_pairing *pairing,
                                      const uint8_t *message, size_t message_size);
/* Available only after authenticated bilateral completion. */
int falafel_pairing_get_peer_public_key(const falafel_pairing *pairing,
                                        uint8_t public_key[FALAFEL_PAIRING_IDENTITY_SIZE]);

#endif
