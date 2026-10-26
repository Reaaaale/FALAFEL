#include "falafel_crypto.h"

#include <stdint.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "falafel_crypto_test";

static void log_stack_watermark(const char *phase) {
    ESP_LOGI(TAG, "main stack minimum free after %s: %u bytes", phase,
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static int decode_hex(const char *hex, uint8_t *output, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        int high = hex_value(hex[i * 2u]);
        int low = hex_value(hex[i * 2u + 1u]);
        if (high < 0 || low < 0) return 0;
        output[i] = (uint8_t)((high << 4) | low);
    }
    return hex[size * 2u] == '\0';
}

static int known_vector_tests(falafel_crypto_context_t *context) {
    static const uint8_t ikm[22] = {
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b
    };
    static const uint8_t salt[13] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    static const uint8_t info[10] = {0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9};
    static const uint8_t expected[42] = {
        0x3c,0xb2,0x5f,0x25,0xfa,0xac,0xd5,0x7a,0x90,0x43,0x4f,0x64,0xd0,0x36,
        0x2f,0x2a,0x2d,0x2d,0x0a,0x90,0xcf,0x1a,0x5a,0x4c,0x5d,0xb0,0x2d,0x56,
        0xec,0xc4,0xc5,0xbf,0x34,0x00,0x72,0x08,0xd5,0xb8,0x87,0x18,0x58,0x65
    };
    static const uint8_t plaintext[16] = {0};
    static const uint8_t expected_ciphertext[16] = {
        0xce,0xa7,0x40,0x3d,0x4d,0x60,0x6b,0x6e,
        0x07,0x4e,0xc5,0xd3,0xba,0xf3,0x9d,0x18
    };
    static const uint8_t expected_tag[16] = {
        0xd0,0xd1,0xc8,0xa7,0x99,0x99,0x6b,0xf0,
        0x26,0x5b,0x98,0xb5,0xd4,0x8a,0xb9,0x19
    };
    uint8_t output[42], key[32] = {0}, nonce[12] = {0}, tag[16];
    uint8_t private_a[32], public_a[32], private_b[32], public_b[32], shared_a[32], shared_b[32];
    if (falafel_crypto_hkdf_sha256(salt, sizeof(salt), ikm, sizeof(ikm), info,
                                   sizeof(info), output, sizeof(output)) != FALAFEL_CRYPTO_OK ||
        memcmp(output, expected, sizeof(output)) != 0) return 0;
    if (falafel_crypto_aead_encrypt(key, sizeof(key), nonce, sizeof(nonce), NULL, 0,
                                    plaintext, sizeof(plaintext), output, sizeof(output),
                                    tag, sizeof(tag)) != FALAFEL_CRYPTO_OK ||
        memcmp(output, expected_ciphertext, sizeof(expected_ciphertext)) != 0 ||
        memcmp(tag, expected_tag, sizeof(tag)) != 0) return 0;
    if (!decode_hex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", private_a, 32) ||
        !decode_hex("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f", public_b, 32) ||
        !decode_hex("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742", output, 32) ||
        falafel_crypto_x25519_shared(context, private_a, 32, public_b, 32, shared_a, 32) != FALAFEL_CRYPTO_OK ||
        !falafel_crypto_constant_time_equal(shared_a, output, 32)) return 0;
    if (falafel_crypto_x25519_generate(context, private_a, 32, public_a, 32) != FALAFEL_CRYPTO_OK ||
        falafel_crypto_x25519_generate(context, private_b, 32, public_b, 32) != FALAFEL_CRYPTO_OK ||
        falafel_crypto_x25519_shared(context, private_a, 32, public_b, 32, shared_a, 32) != FALAFEL_CRYPTO_OK ||
        falafel_crypto_x25519_shared(context, private_b, 32, public_a, 32, shared_b, 32) != FALAFEL_CRYPTO_OK ||
        !falafel_crypto_constant_time_equal(shared_a, shared_b, 32)) return 0;
    falafel_crypto_secure_zero(private_a, sizeof(private_a));
    falafel_crypto_secure_zero(private_b, sizeof(private_b));
    falafel_crypto_secure_zero(shared_a, sizeof(shared_a));
    falafel_crypto_secure_zero(shared_b, sizeof(shared_b));
    return 1;
}

void app_main(void) {
    falafel_crypto_context_t *context = NULL;
    int initialized = 0;
    int passed = 0;
    int64_t started = esp_timer_get_time();
    size_t free_before = esp_get_free_heap_size();

    ESP_LOGI(TAG, "backend: %s", falafel_crypto_backend_version());
    ESP_LOGI(TAG, "configured main stack: %u bytes; crypto context: %u bytes",
             (unsigned)CONFIG_ESP_MAIN_TASK_STACK_SIZE,
             (unsigned)sizeof(*context));
    log_stack_watermark("startup");

    context = heap_caps_malloc(sizeof(*context), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (context == NULL) {
        ESP_LOGE(TAG, "crypto context allocation: FAILED");
        goto finished;
    }

    if (falafel_crypto_init(context) != FALAFEL_CRYPTO_OK) {
        ESP_LOGE(TAG, "secure random initialization: FAILED");
        goto finished;
    }
    initialized = 1;
    log_stack_watermark("crypto initialization");

    if (!known_vector_tests(context)) {
        goto finished;
    }
    passed = 1;
    log_stack_watermark("known-vector tests");

finished:
    if (initialized) falafel_crypto_free(context);
    heap_caps_free(context);

    if (passed) {
        ESP_LOGI(TAG, "cryptographic self-tests: PASS");
    } else {
        ESP_LOGE(TAG, "cryptographic self-tests: FAIL");
    }
    ESP_LOGI(TAG, "elapsed: %lld us", (long long)(esp_timer_get_time() - started));
    ESP_LOGI(TAG, "free heap before/after: %u/%u bytes; largest block: %u bytes",
             (unsigned)free_before, (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    log_stack_watermark("cleanup");
}
