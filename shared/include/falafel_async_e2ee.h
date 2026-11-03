#ifndef FALAFEL_ASYNC_E2EE_H
#define FALAFEL_ASYNC_E2EE_H
#include "falafel_crypto.h"
#include "falafel_pairing_persistence.h"
#define FALAFEL_ASYNC_HEADER_SIZE 86u
#define FALAFEL_ASYNC_OVERHEAD 134u
#define FALAFEL_ASYNC_MAX_PLAINTEXT 122u
#define FALAFEL_ASYNC_MAX_ENVELOPE 256u
/* Noise_K_25519_AESGCM_SHA256, one fresh handshake per envelope.
 * State must come from trusted, bilaterally confirmed pairing persistence.
 * No replay tracking. Message IDs are opaque 16-byte application identifiers.
 * Buffers must not overlap each other or state/context; NULL plaintext allowed
 * only at length zero. Output pointer is required even for empty plaintext.
 * Failure sets *written=0 and clears output up to min(capacity, module limit).
 * Decrypt publishes message_id only on success (clears it on failure).
 * Caller serializes access to the initialized crypto context.
 */
falafel_crypto_result_t falafel_async_encrypt(falafel_crypto_context_t *crypto,
    const falafel_pairing_persistent_state *identity, const uint8_t message_id[16],
    const uint8_t *plain, size_t size, uint8_t *out, size_t capacity, size_t *written);
falafel_crypto_result_t falafel_async_decrypt(falafel_crypto_context_t *crypto,
    const falafel_pairing_persistent_state *identity, const uint8_t *envelope,
    size_t size, uint8_t *out, size_t capacity, size_t *written, uint8_t message_id[16]);
#endif
