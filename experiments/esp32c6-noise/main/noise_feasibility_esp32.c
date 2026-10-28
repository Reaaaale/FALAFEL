#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/*
 * Reuse the shared feasibility helpers without changing their cryptographic
 * operations.  The ESP32-C6 runner below only changes benchmark scheduling.
 */
#define noise_feasibility_run noise_feasibility_run_shared
#include "../../noise-feasibility/noise_feasibility.c"
#undef noise_feasibility_run

int noise_feasibility_run(void)
{
    Session first, second;
    uint64_t elapsed = 0;
    const unsigned rounds = 100;

    assert(complete(&first) == 0);
    assert(transport(first.i_send, first.r_recv, 0) == 0);
    assert(transport(first.r_send, first.i_recv, 0) == 0);
    assert(transport(first.i_send, first.r_recv, 1) == 0);
    {
        uint8_t data[1]; NoiseBuffer b, p;
        noise_buffer_set_output(b, data, sizeof(data));
        noise_buffer_set_input(p, data, 0);
        assert(noise_handshakestate_write_message(first.initiator, &b, &p) ==
               NOISE_ERROR_INVALID_STATE);
    }
    assert(complete(&second) == 0);
    assert(memcmp(first.messages[0], second.messages[0], first.lengths[0]) != 0);
    cleanup(&first); cleanup(&second);

    assert(rejected_variant(0, 1) == 0);
    assert(rejected_variant(1, 2) == 0);
    assert(rejected_variant(2, 2) == 0);
    assert(rejected_variant(0, 3) == 0);
    assert(rejected_variant(0, 4) == 0);
    assert(rejected_variant(1, 1) == 0);
    assert(rejected_variant(2, 1) == 0);
    assert(altered_message1_eventually_rejected() == 0);
    assert(oversized_message_rejected() == 0);

    assert(setup(&first) == 0);
    {
        uint8_t data[32]; NoiseBuffer b, p;
        noise_buffer_set_output(b, data, sizeof(data));
        noise_buffer_set_input(p, data, 0);
        assert(noise_handshakestate_write_message(first.responder, &b, &p) ==
               NOISE_ERROR_INVALID_STATE);
    }
    cleanup(&first);

    assert(complete(&first) == 0);
    {
        NoiseCipherState *wrong = NULL;
        uint8_t data[64] = "wrong key";
        uint8_t key[32] = {0};
        NoiseBuffer b;
        assert(noise_cipherstate_new_by_name(&wrong, "AESGCM") == NOISE_ERROR_NONE);
        assert(noise_cipherstate_init_key(wrong, key, sizeof(key)) == NOISE_ERROR_NONE);
        noise_buffer_set_inout(b, data, sizeof("wrong key"), sizeof(data));
        assert(noise_cipherstate_encrypt(first.i_send, &b) == NOISE_ERROR_NONE);
        assert(noise_cipherstate_decrypt(wrong, &b) == NOISE_ERROR_MAC_FAILURE);
        noise_cipherstate_free(wrong);
    }
    cleanup(&first);

    for (unsigned i = 0; i < rounds; ++i) {
        uint64_t start = now_ns();
        assert(complete(&first) == 0);
        cleanup(&first);
        elapsed += now_ns() - start;
        if (i + 1u < rounds) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    printf("Noise-C revision: cfe25410979a87391bb9ac8d4d4bef64e9f268c6\n");
    printf("suite: %s\n", PROTOCOL);
    printf("tests: PASS\nhandshakes: %u\naverage_handshake_us: %.2f\n", rounds,
           (double)elapsed / (double)rounds / 1000.0);
    return 0;
}
