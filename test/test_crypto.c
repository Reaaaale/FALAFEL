#include "falafel_crypto.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static void decode_hex(const char *hex, uint8_t *output, size_t size) {
    size_t i;
    assert(strlen(hex) == size * 2u);
    for (i = 0; i < size; ++i) {
        int high = hex_value(hex[i * 2u]);
        int low = hex_value(hex[i * 2u + 1u]);
        assert(high >= 0 && low >= 0);
        output[i] = (uint8_t)((high << 4) | low);
    }
}

static void test_x25519(falafel_crypto_context_t *context) {
    uint8_t alice_private[32], alice_public[32], bob_private[32], bob_public[32];
    uint8_t expected[32], shared_a[32], shared_b[32];
    decode_hex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", alice_private, 32);
    decode_hex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb", bob_private, 32);
    decode_hex("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a", alice_public, 32);
    decode_hex("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f", bob_public, 32);
    decode_hex("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742", expected, 32);
    assert(falafel_crypto_x25519_shared(context, alice_private, 32, bob_public, 32,
                                        shared_a, 32) == FALAFEL_CRYPTO_OK);
    assert(falafel_crypto_x25519_shared(context, bob_private, 32, alice_public, 32,
                                        shared_b, 32) == FALAFEL_CRYPTO_OK);
    assert(falafel_crypto_constant_time_equal(shared_a, expected, 32));
    assert(falafel_crypto_constant_time_equal(shared_a, shared_b, 32));
    assert(falafel_crypto_x25519_public_from_private(context, alice_private, 32, shared_a, 32) ==
           FALAFEL_CRYPTO_OK);
    assert(memcmp(shared_a, alice_public, 32) == 0);
}

static void test_hkdf(void) {
    uint8_t ikm[22], salt[13], info[10], output[42], expected[42];
    memset(ikm, 0x0b, sizeof(ikm));
    decode_hex("000102030405060708090a0b0c", salt, sizeof(salt));
    decode_hex("f0f1f2f3f4f5f6f7f8f9", info, sizeof(info));
    decode_hex("3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865", expected, sizeof(expected));
    assert(falafel_crypto_hkdf_sha256(salt, sizeof(salt), ikm, sizeof(ikm), info,
                                      sizeof(info), output, sizeof(output)) == FALAFEL_CRYPTO_OK);
    assert(memcmp(output, expected, sizeof(output)) == 0);
}

static void test_aead(void) {
    uint8_t key[32] = {0}, nonce[12] = {0}, tag[16], expected_tag[16];
    uint8_t zero_block[16] = {0}, expected_ciphertext[16], expected_block_tag[16];
    uint8_t plaintext[] = "Falafel authenticated payload";
    uint8_t ciphertext[sizeof(plaintext)], recovered[sizeof(plaintext)];
    decode_hex("530f8afbc74536b9a963b4f1c4cb738b", expected_tag, 16);
    assert(falafel_crypto_aead_encrypt(key, 32, nonce, 12, NULL, 0, NULL, 0,
                                      NULL, 0, tag, 16) == FALAFEL_CRYPTO_OK);
    assert(memcmp(tag, expected_tag, 16) == 0);
    decode_hex("cea7403d4d606b6e074ec5d3baf39d18", expected_ciphertext, 16);
    decode_hex("d0d1c8a799996bf0265b98b5d48ab919", expected_block_tag, 16);
    assert(falafel_crypto_aead_encrypt(key, 32, nonce, 12, NULL, 0,
                                      zero_block, sizeof(zero_block), recovered,
                                      sizeof(recovered), tag, 16) == FALAFEL_CRYPTO_OK);
    assert(memcmp(recovered, expected_ciphertext, sizeof(expected_ciphertext)) == 0);
    assert(memcmp(tag, expected_block_tag, sizeof(expected_block_tag)) == 0);
    assert(falafel_crypto_aead_encrypt(key, 32, nonce, 12, (const uint8_t *)"aad", 3,
                                      plaintext, sizeof(plaintext), ciphertext,
                                      sizeof(ciphertext), tag, 16) == FALAFEL_CRYPTO_OK);
    assert(falafel_crypto_aead_decrypt(key, 32, nonce, 12, (const uint8_t *)"aad", 3,
                                      ciphertext, sizeof(ciphertext), tag, 16, recovered,
                                      sizeof(recovered)) == FALAFEL_CRYPTO_OK);
    assert(memcmp(plaintext, recovered, sizeof(plaintext)) == 0);
    ciphertext[0] ^= 1u;
    memset(recovered, 0xa5, sizeof(recovered));
    assert(falafel_crypto_aead_decrypt(key, 32, nonce, 12, (const uint8_t *)"aad", 3,
                                      ciphertext, sizeof(ciphertext), tag, 16, recovered,
                                      sizeof(recovered)) == FALAFEL_CRYPTO_AUTH_FAILED);
    for (size_t i = 0; i < sizeof(recovered); ++i) assert(recovered[i] == 0);
    ciphertext[0] ^= 1u;
    tag[0] ^= 1u;
    assert(falafel_crypto_aead_decrypt(key, 32, nonce, 12, (const uint8_t *)"aad", 3,
                                      ciphertext, sizeof(ciphertext), tag, 16, recovered,
                                      sizeof(recovered)) == FALAFEL_CRYPTO_AUTH_FAILED);
}

static void test_validation(falafel_crypto_context_t *context) {
    uint8_t data[64] = {0}, second[64] = {0};
    assert(falafel_crypto_random(context, data, sizeof(data)) == FALAFEL_CRYPTO_OK);
    assert(falafel_crypto_random(context, second, sizeof(second)) == FALAFEL_CRYPTO_OK);
    assert(memcmp(data, second, sizeof(data)) != 0);
    assert(falafel_crypto_random(NULL, data, sizeof(data)) == FALAFEL_CRYPTO_INVALID_ARGUMENT);
    assert(falafel_crypto_x25519_generate(context, data, 31, data + 32, 32) ==
           FALAFEL_CRYPTO_INVALID_ARGUMENT);
    assert(falafel_crypto_hkdf_sha256(NULL, 1, data, 1, NULL, 0, second, 32) ==
           FALAFEL_CRYPTO_INVALID_ARGUMENT);
    assert(falafel_crypto_aead_encrypt(data, 31, data, 12, NULL, 0, data, 1,
                                      second, 1, second + 32, 16) ==
           FALAFEL_CRYPTO_INVALID_ARGUMENT);
    assert(falafel_crypto_aead_encrypt(data, 32, data, 11, NULL, 0, data, 1,
                                      second, 1, second + 32, 16) ==
           FALAFEL_CRYPTO_INVALID_ARGUMENT);
    assert(falafel_crypto_aead_encrypt(data, 32, data, 12, NULL, 0, data, 2,
                                      second, 1, second + 32, 16) ==
           FALAFEL_CRYPTO_INVALID_ARGUMENT);
    assert(!falafel_crypto_constant_time_equal(NULL, data, 1));
}

int main(void) {
    falafel_crypto_context_t context;
    uint8_t private_key[32], public_key[32];
    assert(falafel_crypto_init(&context) == FALAFEL_CRYPTO_OK);
    assert(falafel_crypto_x25519_generate(&context, private_key, sizeof(private_key),
                                          public_key, sizeof(public_key)) == FALAFEL_CRYPTO_OK);
    test_x25519(&context);
    test_hkdf();
    test_aead();
    test_validation(&context);
    falafel_crypto_secure_zero(private_key, sizeof(private_key));
    falafel_crypto_free(&context);
    puts("crypto tests passed");
    return 0;
}
