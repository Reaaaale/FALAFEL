#ifndef FALAFEL_CRYPTO_H
#define FALAFEL_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#define FALAFEL_CRYPTO_X25519_KEY_SIZE 32u
#define FALAFEL_CRYPTO_AES256_KEY_SIZE 32u
#define FALAFEL_CRYPTO_GCM_NONCE_SIZE 12u
#define FALAFEL_CRYPTO_GCM_TAG_SIZE 16u
#define FALAFEL_CRYPTO_CONTEXT_SIZE 1536u

typedef enum {
    FALAFEL_CRYPTO_OK = 0,
    FALAFEL_CRYPTO_INVALID_ARGUMENT,
    FALAFEL_CRYPTO_BUFFER_TOO_SMALL,
    FALAFEL_CRYPTO_AUTH_FAILED,
    FALAFEL_CRYPTO_RANDOM_FAILED,
    FALAFEL_CRYPTO_BACKEND_ERROR
} falafel_crypto_result_t;

typedef union {
    max_align_t alignment;
    uint8_t bytes[FALAFEL_CRYPTO_CONTEXT_SIZE];
} falafel_crypto_context_t;

/* The context owns the platform entropy/DRBG state. Initialize once before use
 * and free it before release. AEAD is intentionally stateless: callers must
 * guarantee that a nonce is never reused with the same key. */

falafel_crypto_result_t falafel_crypto_init(falafel_crypto_context_t *context);
void falafel_crypto_free(falafel_crypto_context_t *context);
falafel_crypto_result_t falafel_crypto_random(falafel_crypto_context_t *context,
                                              uint8_t *output, size_t output_size);
falafel_crypto_result_t falafel_crypto_x25519_generate(
    falafel_crypto_context_t *context, uint8_t *private_key, size_t private_key_size,
    uint8_t *public_key, size_t public_key_size);
falafel_crypto_result_t falafel_crypto_x25519_public_from_private(
    falafel_crypto_context_t *context,
    const uint8_t *private_key, size_t private_key_size,
    uint8_t *public_key, size_t public_key_size);
falafel_crypto_result_t falafel_crypto_x25519_shared(
    falafel_crypto_context_t *context,
    const uint8_t *private_key, size_t private_key_size,
    const uint8_t *peer_public_key, size_t peer_public_key_size,
    uint8_t *shared_secret, size_t shared_secret_size);
falafel_crypto_result_t falafel_crypto_hkdf_sha256(
    const uint8_t *salt, size_t salt_size, const uint8_t *input_key_material,
    size_t input_key_material_size, const uint8_t *info, size_t info_size,
    uint8_t *output, size_t output_size);
falafel_crypto_result_t falafel_crypto_aead_encrypt(
    const uint8_t *key, size_t key_size, const uint8_t *nonce, size_t nonce_size,
    const uint8_t *additional_data, size_t additional_data_size,
    const uint8_t *plaintext, size_t plaintext_size,
    uint8_t *ciphertext, size_t ciphertext_capacity,
    uint8_t *tag, size_t tag_size);
falafel_crypto_result_t falafel_crypto_aead_decrypt(
    const uint8_t *key, size_t key_size, const uint8_t *nonce, size_t nonce_size,
    const uint8_t *additional_data, size_t additional_data_size,
    const uint8_t *ciphertext, size_t ciphertext_size,
    const uint8_t *tag, size_t tag_size,
    uint8_t *plaintext, size_t plaintext_capacity);
int falafel_crypto_constant_time_equal(const uint8_t *left, const uint8_t *right,
                                       size_t size);
void falafel_crypto_secure_zero(void *data, size_t size);
const char *falafel_crypto_backend_version(void);

#endif
