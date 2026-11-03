#include "falafel_async_e2ee.h"
#include <string.h>

/* Minimal single-message Noise K processing; no transport CipherState retained.
 * All primitives, erasure, DH validation and entropy use the shared provider. */
typedef struct {
    uint8_t ck[32], h[32], key[32], dh[32], expanded[64];
    uint8_t ephemeral[32], public_key[32], hash_input[118];
} secrets;
static falafel_crypto_result_t mix_hash(secrets *s, const uint8_t *p, size_t n)
{
    memcpy(s->hash_input, s->h, 32);
    memcpy(s->hash_input + 32, p, n);
    return falafel_crypto_sha256(s->hash_input, 32 + n, s->h);
}
static falafel_crypto_result_t mix_key(secrets *s, falafel_crypto_context_t *c,
                                     const uint8_t *priv, const uint8_t *pub)
{
    falafel_crypto_result_t r = falafel_crypto_x25519_shared(c, priv, 32, pub, 32, s->dh, 32);
    uint8_t zero[32] = {0};
    if (r != FALAFEL_CRYPTO_OK) return r;
    if (falafel_crypto_constant_time_equal(s->dh, zero, 32)) return FALAFEL_CRYPTO_AUTH_FAILED;
    /* Noise HKDF(ck, dh, 2) = RFC5869 HKDF with empty info, 64 bytes. */
    r = falafel_crypto_hkdf_sha256(s->ck, 32, s->dh, 32, NULL, 0, s->expanded, 64);
    if (r == FALAFEL_CRYPTO_OK) {
        memcpy(s->ck, s->expanded, 32); memcpy(s->key, s->expanded + 32, 32);
    }
    return r;
}
static falafel_crypto_result_t setup(secrets *s, falafel_crypto_context_t *c,
    const falafel_pairing_persistent_state *id, const uint8_t *header, int sending)
{
    static const uint8_t name[] = "Noise_K_25519_AESGCM_SHA256";
    uint8_t zero[32] = {0};
    falafel_crypto_result_t r;
    if (!id || id->has_pairing != 1 || id->generation == 0 ||
        falafel_crypto_constant_time_equal(id->local_private, zero, 32) ||
        falafel_crypto_constant_time_equal(id->peer_public, zero, 32))
        return FALAFEL_CRYPTO_INVALID_ARGUMENT;
    r = falafel_crypto_x25519_public_from_private(c, id->local_private, 32, s->public_key, 32);
    if (r != FALAFEL_CRYPTO_OK) return r;
    if (!falafel_crypto_constant_time_equal(s->public_key, id->local_public, 32) ||
        !falafel_crypto_constant_time_equal(header + (sending ? 6 : 38), id->local_public, 32) ||
        !falafel_crypto_constant_time_equal(header + (sending ? 38 : 6), id->peer_public, 32))
        return FALAFEL_CRYPTO_AUTH_FAILED;
    memcpy(s->h, name, sizeof(name)-1); memcpy(s->ck, s->h, 32);
    /* Prologue = entire envelope header; pre-messages = initiator s, responder s. */
    r = mix_hash(s, header, FALAFEL_ASYNC_HEADER_SIZE);
    if (r == FALAFEL_CRYPTO_OK) r = mix_hash(s, header + 6, 32);
    if (r == FALAFEL_CRYPTO_OK) r = mix_hash(s, header + 38, 32);
    return r;
}
static void clear_output(uint8_t *out, size_t capacity, size_t limit)
{ if (out) falafel_crypto_secure_zero(out, capacity < limit ? capacity : limit); }

falafel_crypto_result_t falafel_async_encrypt(falafel_crypto_context_t *c,
    const falafel_pairing_persistent_state *id, const uint8_t mid[16],
    const uint8_t *plain, size_t size, uint8_t *out, size_t cap, size_t *written)
{
    secrets s = {0}; uint8_t wire[FALAFEL_ASYNC_MAX_ENVELOPE] = {0}, nonce[12] = {0};
    falafel_crypto_result_t r = FALAFEL_CRYPTO_INVALID_ARGUMENT;
    if (written) *written = 0;
    if (!c || !id || !mid || (!plain && size) || !out || !written || size > FALAFEL_ASYNC_MAX_PLAINTEXT) goto done;
    if (cap < size + FALAFEL_ASYNC_OVERHEAD) { r = FALAFEL_CRYPTO_BUFFER_TOO_SMALL; goto done; }
    memcpy(wire, "FAE", 3); wire[3] = 1; wire[4] = 1; wire[5] = (uint8_t)size;
    memcpy(wire + 6, id->local_public, 32); memcpy(wire + 38, id->peer_public, 32);
    memcpy(wire + 70, mid, 16);
    r = setup(&s, c, id, wire, 1); if (r) goto done;
    r = falafel_crypto_x25519_generate(c, s.ephemeral, 32, wire + 86, 32); if (r) goto done;
    r = mix_hash(&s, wire + 86, 32); if (r) goto done;
    r = mix_key(&s, c, s.ephemeral, id->peer_public); if (r) goto done;
    r = mix_key(&s, c, id->local_private, id->peer_public); if (r) goto done;
    r = falafel_crypto_aead_encrypt(s.key, 32, nonce, 12, s.h, 32, plain, size,
                                   wire + 118, size, wire + 118 + size, 16);
    if (!r) { memcpy(out, wire, size + FALAFEL_ASYNC_OVERHEAD); *written = size + FALAFEL_ASYNC_OVERHEAD; }
done:
    if (r) clear_output(out, cap, FALAFEL_ASYNC_MAX_ENVELOPE);
    falafel_crypto_secure_zero(&s, sizeof(s)); falafel_crypto_secure_zero(wire, sizeof(wire));
    return r;
}
falafel_crypto_result_t falafel_async_decrypt(falafel_crypto_context_t *c,
    const falafel_pairing_persistent_state *id, const uint8_t *wire,
    size_t size, uint8_t *out, size_t cap, size_t *written, uint8_t mid[16])
{
    secrets s = {0}; uint8_t plain[FALAFEL_ASYNC_MAX_PLAINTEXT] = {0}, nonce[12] = {0};
    size_t n = 0; falafel_crypto_result_t r = FALAFEL_CRYPTO_INVALID_ARGUMENT;
    if (written) *written = 0;
    if (mid) memset(mid, 0, 16);
    if (!c || !id || !wire || !out || !written || !mid || size < FALAFEL_ASYNC_OVERHEAD ||
        size > FALAFEL_ASYNC_MAX_ENVELOPE || memcmp(wire, "FAE", 3) || wire[3] != 1 || wire[4] != 1 ||
        wire[5] > FALAFEL_ASYNC_MAX_PLAINTEXT || size != FALAFEL_ASYNC_OVERHEAD + wire[5]) goto done;
    n = wire[5];
    if (cap < n) { r = FALAFEL_CRYPTO_BUFFER_TOO_SMALL; goto done; }
    r = setup(&s, c, id, wire, 0); if (r) goto done;
    r = mix_hash(&s, wire + 86, 32); if (r) goto done;
    r = mix_key(&s, c, id->local_private, wire + 86); if (r) goto done;
    r = mix_key(&s, c, id->local_private, id->peer_public); if (r) goto done;
    r = falafel_crypto_aead_decrypt(s.key, 32, nonce, 12, s.h, 32, wire + 118, n,
                                   wire + 118 + n, 16, plain, sizeof(plain));
    if (!r) { memcpy(out, plain, n); memcpy(mid, wire + 70, 16); *written = n; }
done:
    if (r) clear_output(out, cap, FALAFEL_ASYNC_MAX_PLAINTEXT);
    falafel_crypto_secure_zero(&s, sizeof(s)); falafel_crypto_secure_zero(plain, sizeof(plain));
    return r;
}
