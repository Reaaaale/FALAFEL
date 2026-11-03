#ifndef FALAFEL_E2EE_H
#define FALAFEL_E2EE_H
#include "falafel_pairing_persistence.h"

typedef struct falafel_e2ee falafel_e2ee;
typedef enum { FALAFEL_E2EE_HANDSHAKE, FALAFEL_E2EE_READY, FALAFEL_E2EE_CLOSED } falafel_e2ee_state;
int falafel_e2ee_new(falafel_e2ee **out, falafel_pairing_role role, const falafel_pairing_persistent_state *identity);
void falafel_e2ee_free(falafel_e2ee *session);
void falafel_e2ee_close(falafel_e2ee *session);
falafel_e2ee_state falafel_e2ee_status(const falafel_e2ee *session);
int falafel_e2ee_write_handshake(falafel_e2ee *s, uint8_t *out, size_t capacity, size_t *size);
int falafel_e2ee_read_handshake(falafel_e2ee *s, const uint8_t *data, size_t size);
/* Wire format: big-endian sequence (8 bytes), followed by ciphertext and tag. */
int falafel_e2ee_encrypt(falafel_e2ee *s, const uint8_t *plain, size_t size, uint8_t *out, size_t capacity, size_t *out_size);
int falafel_e2ee_decrypt(falafel_e2ee *s, const uint8_t *wire, size_t size, uint8_t *out, size_t capacity, size_t *out_size);
#endif
