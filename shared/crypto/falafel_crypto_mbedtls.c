#include "falafel_crypto.h"

#include <limits.h>
#include <string.h>

#include <mbedtls/constant_time.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/gcm.h>
#include <mbedtls/hkdf.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/private_access.h>
#include <mbedtls/version.h>

typedef struct {
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    uint32_t initialized;
} crypto_internal_t;

_Static_assert(sizeof(crypto_internal_t) <= FALAFEL_CRYPTO_CONTEXT_SIZE,
               "FALAFEL_CRYPTO_CONTEXT_SIZE is too small");

static crypto_internal_t *internal(falafel_crypto_context_t *context) {
    return (crypto_internal_t *)(void *)context->bytes;
}

static int valid_buffer(const void *buffer, size_t size) {
    return size == 0u || buffer != NULL;
}

static falafel_crypto_result_t load_private(const uint8_t *private_key,
                                            mbedtls_ecp_keypair *key) {
    mbedtls_ecp_keypair_init(key);
    if (mbedtls_ecp_read_key(MBEDTLS_ECP_DP_CURVE25519, key, private_key,
                             FALAFEL_CRYPTO_X25519_KEY_SIZE) != 0) {
        mbedtls_ecp_keypair_free(key);
        return FALAFEL_CRYPTO_INVALID_ARGUMENT;
    }
    return FALAFEL_CRYPTO_OK;
}

falafel_crypto_result_t falafel_crypto_init(falafel_crypto_context_t *context) {
    static const unsigned char personalization[] = "falafel-crypto-v1";
    crypto_internal_t *state;
    if (context == NULL) return FALAFEL_CRYPTO_INVALID_ARGUMENT;
    memset(context, 0, sizeof(*context));
    state = internal(context);
    mbedtls_entropy_init(&state->entropy);
    mbedtls_ctr_drbg_init(&state->drbg);
    if (mbedtls_ctr_drbg_seed(&state->drbg, mbedtls_entropy_func, &state->entropy,
                              personalization, sizeof(personalization) - 1u) != 0) {
        falafel_crypto_free(context);
        return FALAFEL_CRYPTO_RANDOM_FAILED;
    }
    state->initialized = 0x46414c43u;
    return FALAFEL_CRYPTO_OK;
}

void falafel_crypto_free(falafel_crypto_context_t *context) {
    crypto_internal_t *state;
    if (context == NULL) return;
    state = internal(context);
    mbedtls_ctr_drbg_free(&state->drbg);
    mbedtls_entropy_free(&state->entropy);
    mbedtls_platform_zeroize(context, sizeof(*context));
}

falafel_crypto_result_t falafel_crypto_random(falafel_crypto_context_t *context,
                                              uint8_t *output, size_t output_size) {
    if (context == NULL || !valid_buffer(output, output_size) ||
        internal(context)->initialized != 0x46414c43u)
        return FALAFEL_CRYPTO_INVALID_ARGUMENT;
    if (mbedtls_ctr_drbg_random(&internal(context)->drbg, output, output_size) != 0)
        return FALAFEL_CRYPTO_RANDOM_FAILED;
    return FALAFEL_CRYPTO_OK;
}

falafel_crypto_result_t falafel_crypto_x25519_generate(
    falafel_crypto_context_t *context, uint8_t *private_key, size_t private_key_size,
    uint8_t *public_key, size_t public_key_size) {
    mbedtls_ecp_keypair key;
    size_t written = 0;
    int rc;
    if (context == NULL || private_key == NULL || public_key == NULL ||
        private_key_size != 32u || public_key_size != 32u ||
        internal(context)->initialized != 0x46414c43u)
        return FALAFEL_CRYPTO_INVALID_ARGUMENT;
    mbedtls_ecp_keypair_init(&key);
    rc = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_CURVE25519, &key,
                             mbedtls_ctr_drbg_random, &internal(context)->drbg);
    if (rc == 0) rc = mbedtls_ecp_write_key_ext(&key, &written, private_key, private_key_size);
    if (rc == 0 && written == 32u)
        rc = mbedtls_ecp_write_public_key(&key, MBEDTLS_ECP_PF_UNCOMPRESSED,
                                          &written, public_key, public_key_size);
    mbedtls_ecp_keypair_free(&key);
    if (rc != 0 || written != 32u) {
        falafel_crypto_secure_zero(private_key, private_key_size);
        falafel_crypto_secure_zero(public_key, public_key_size);
        return FALAFEL_CRYPTO_BACKEND_ERROR;
    }
    return FALAFEL_CRYPTO_OK;
}

falafel_crypto_result_t falafel_crypto_x25519_public_from_private(
    falafel_crypto_context_t *context, const uint8_t *private_key, size_t private_key_size,
    uint8_t *public_key, size_t public_key_size) {
    mbedtls_ecp_keypair key;
    size_t written = 0;
    int rc;
    if (context == NULL || private_key == NULL || public_key == NULL ||
        private_key_size != 32u || public_key_size != 32u ||
        internal(context)->initialized != 0x46414c43u) return FALAFEL_CRYPTO_INVALID_ARGUMENT;
    if (load_private(private_key, &key) != FALAFEL_CRYPTO_OK)
        return FALAFEL_CRYPTO_INVALID_ARGUMENT;
    rc = mbedtls_ecp_keypair_calc_public(&key, mbedtls_ctr_drbg_random,
                                          &internal(context)->drbg);
    if (rc == 0) rc = mbedtls_ecp_write_public_key(&key, MBEDTLS_ECP_PF_UNCOMPRESSED,
                                                   &written, public_key, public_key_size);
    mbedtls_ecp_keypair_free(&key);
    return rc == 0 && written == 32u ? FALAFEL_CRYPTO_OK : FALAFEL_CRYPTO_BACKEND_ERROR;
}

falafel_crypto_result_t falafel_crypto_x25519_shared(
    falafel_crypto_context_t *context, const uint8_t *private_key, size_t private_key_size,
    const uint8_t *peer_public_key, size_t peer_public_key_size,
    uint8_t *shared_secret, size_t shared_secret_size) {
    mbedtls_ecp_keypair key;
    mbedtls_ecp_point peer;
    mbedtls_mpi secret;
    int rc;
    if (context == NULL || private_key == NULL || peer_public_key == NULL ||
        shared_secret == NULL || private_key_size != 32u || peer_public_key_size != 32u ||
        shared_secret_size != 32u || internal(context)->initialized != 0x46414c43u)
        return FALAFEL_CRYPTO_INVALID_ARGUMENT;
    if (load_private(private_key, &key) != FALAFEL_CRYPTO_OK)
        return FALAFEL_CRYPTO_INVALID_ARGUMENT;
    mbedtls_ecp_point_init(&peer);
    mbedtls_mpi_init(&secret);
    rc = mbedtls_ecp_point_read_binary(&key.MBEDTLS_PRIVATE(grp), &peer,
                                       peer_public_key, peer_public_key_size);
    if (rc == 0) rc = mbedtls_ecdh_compute_shared(&key.MBEDTLS_PRIVATE(grp), &secret, &peer,
                                                   &key.MBEDTLS_PRIVATE(d),
                                                   mbedtls_ctr_drbg_random,
                                                   &internal(context)->drbg);
    if (rc == 0) rc = mbedtls_mpi_write_binary_le(&secret, shared_secret, shared_secret_size);
    mbedtls_mpi_free(&secret);
    mbedtls_ecp_point_free(&peer);
    mbedtls_ecp_keypair_free(&key);
    if (rc != 0) {
        falafel_crypto_secure_zero(shared_secret, shared_secret_size);
        return FALAFEL_CRYPTO_BACKEND_ERROR;
    }
    return FALAFEL_CRYPTO_OK;
}

falafel_crypto_result_t falafel_crypto_hkdf_sha256(
    const uint8_t *salt, size_t salt_size, const uint8_t *ikm, size_t ikm_size,
    const uint8_t *info, size_t info_size, uint8_t *output, size_t output_size) {
    const mbedtls_md_info_t *md;
    if (!valid_buffer(salt, salt_size) || !valid_buffer(ikm, ikm_size) ||
        !valid_buffer(info, info_size) || !valid_buffer(output, output_size) ||
        ikm_size == 0u || output_size == 0u || output_size > 255u * 32u)
        return FALAFEL_CRYPTO_INVALID_ARGUMENT;
    md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (md == NULL || mbedtls_hkdf(md, salt, salt_size, ikm, ikm_size, info,
                                   info_size, output, output_size) != 0)
        return FALAFEL_CRYPTO_BACKEND_ERROR;
    return FALAFEL_CRYPTO_OK;
}

falafel_crypto_result_t falafel_crypto_aead_encrypt(
    const uint8_t *key, size_t key_size, const uint8_t *nonce, size_t nonce_size,
    const uint8_t *aad, size_t aad_size, const uint8_t *plaintext, size_t plaintext_size,
    uint8_t *ciphertext, size_t ciphertext_capacity, uint8_t *tag, size_t tag_size) {
    mbedtls_gcm_context gcm;
    int rc;
    if (key == NULL || key_size != 32u || nonce == NULL || nonce_size != 12u ||
        !valid_buffer(aad, aad_size) || !valid_buffer(plaintext, plaintext_size) ||
        !valid_buffer(ciphertext, plaintext_size) || ciphertext_capacity < plaintext_size ||
        tag == NULL || tag_size != 16u) return FALAFEL_CRYPTO_INVALID_ARGUMENT;
    mbedtls_gcm_init(&gcm);
    rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256u);
    if (rc == 0) rc = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT,
                                                plaintext_size, nonce, nonce_size,
                                                aad, aad_size, plaintext, ciphertext,
                                                tag_size, tag);
    mbedtls_gcm_free(&gcm);
    return rc == 0 ? FALAFEL_CRYPTO_OK : FALAFEL_CRYPTO_BACKEND_ERROR;
}

falafel_crypto_result_t falafel_crypto_aead_decrypt(
    const uint8_t *key, size_t key_size, const uint8_t *nonce, size_t nonce_size,
    const uint8_t *aad, size_t aad_size, const uint8_t *ciphertext, size_t ciphertext_size,
    const uint8_t *tag, size_t tag_size, uint8_t *plaintext, size_t plaintext_capacity) {
    mbedtls_gcm_context gcm;
    int rc;
    if (key == NULL || key_size != 32u || nonce == NULL || nonce_size != 12u ||
        !valid_buffer(aad, aad_size) || !valid_buffer(ciphertext, ciphertext_size) ||
        tag == NULL || tag_size != 16u || !valid_buffer(plaintext, ciphertext_size) ||
        plaintext_capacity < ciphertext_size) return FALAFEL_CRYPTO_INVALID_ARGUMENT;
    mbedtls_gcm_init(&gcm);
    rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256u);
    if (rc == 0) rc = mbedtls_gcm_auth_decrypt(&gcm, ciphertext_size, nonce, nonce_size,
                                               aad, aad_size, tag, tag_size,
                                               ciphertext, plaintext);
    mbedtls_gcm_free(&gcm);
    if (rc == MBEDTLS_ERR_GCM_AUTH_FAILED) {
        falafel_crypto_secure_zero(plaintext, ciphertext_size);
        return FALAFEL_CRYPTO_AUTH_FAILED;
    }
    return rc == 0 ? FALAFEL_CRYPTO_OK : FALAFEL_CRYPTO_BACKEND_ERROR;
}

int falafel_crypto_constant_time_equal(const uint8_t *left, const uint8_t *right,
                                       size_t size) {
    if (!valid_buffer(left, size) || !valid_buffer(right, size)) return 0;
    return mbedtls_ct_memcmp(left, right, size) == 0;
}

void falafel_crypto_secure_zero(void *data, size_t size) {
    if (data != NULL && size > 0u) mbedtls_platform_zeroize(data, size);
}

const char *falafel_crypto_backend_version(void) { return MBEDTLS_VERSION_STRING_FULL; }
