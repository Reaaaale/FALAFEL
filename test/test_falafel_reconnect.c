#include <assert.h>
#include <stdio.h>
#include "falafel_reconnect.h"
#include <stdint.h>

static void test_init_valid(void) {

    falafel_reconnect_t reconnect;

    int result = falafel_reconnect_init(&reconnect, 1000, 16000);
    assert(result == 0);
    assert(reconnect.attempts == 0);
    assert(reconnect.initial_delay_ms == 1000);
    assert(reconnect.max_delay_ms == 16000);
    assert(reconnect.current_delay_ms == 1000);


}

static void test_init_null (void) {
    int result = falafel_reconnect_init(NULL, 1000, 16000);
    assert(result == -1);
    //return 0;
}

static void test_init_zero_delay (void) {
    falafel_reconnect_t reconnect;
    int result = falafel_reconnect_init(&reconnect, 0, 16000);
    assert(result == -1);
    //return 0;
}

static void test_init_invalid_max(void) {
    falafel_reconnect_t reconnect;
    int result = falafel_reconnect_init(&reconnect, 1000, 500);
    assert(result == -1);
    //return 0;
}

static void test_init_preserves_state_on_error(void) {
    falafel_reconnect_t reconnect;
    int result = falafel_reconnect_init(&reconnect, 1000, 16000);
    assert(result == 0);
    reconnect.attempts = 5;
    reconnect.current_delay_ms = 16000;
    int result2 = falafel_reconnect_init(&reconnect, 0, 16000);
    assert(result2 == -1);

    //assert(result == 0);
    assert(reconnect.attempts == 5);
    assert(reconnect.initial_delay_ms == 1000);
    assert(reconnect.max_delay_ms == 16000);
    assert(reconnect.current_delay_ms == 16000);
    
}

static void test_next_delay_sequence(void) {
    falafel_reconnect_t reconnect;
    int result = falafel_reconnect_init(&reconnect, 1000, 16000);
    assert(result == 0);

    uint32_t delay_ms;
    for (uint32_t i = 0; i < 6; i++) {
        result = falafel_reconnect_next_delay(&reconnect, &delay_ms);
        assert(result == 0);
        uint32_t expected = 1000U << i;
        if (expected > 16000U) {
            expected = 16000U;
        }
        assert(delay_ms == expected);
        //printf("Attempt %u: Delay = %u ms\n", i + 1, delay_ms);
    }

    // Test that it caps at max_delay_ms
    result = falafel_reconnect_next_delay(&reconnect, &delay_ms);
    assert(result == 0);
    assert(delay_ms == 16000); // Should cap at max_delay_ms

    // Test that it stays at max_delay_ms
    result = falafel_reconnect_next_delay(&reconnect, &delay_ms);
    assert(result == 0);
    assert(delay_ms == 16000); // Should still be max_delay_ms
}

static void test_next_delay_null(void) {
    falafel_reconnect_t reconnect;
    uint32_t delay_ms = 12345;
    int result = falafel_reconnect_init(&reconnect, 1000, 16000);
    assert(result == 0);
    // reconnect == NULL, delay_ms NULL, entrambi NULL

    //uint32_t delay_ms;
    result = falafel_reconnect_next_delay(NULL, &delay_ms);
    assert(result == -1);

    result = falafel_reconnect_next_delay(&reconnect, NULL);
    assert(result == -1);

    result = falafel_reconnect_next_delay(NULL, NULL);
    assert(result == -1);
    assert(delay_ms == 12345); // Ensure delay_ms is not modified when NULL is passed
    assert(reconnect.attempts == 0); // Ensure attempts is not modified when NULL is passed
    assert(reconnect.current_delay_ms == 1000); // Ensure current_delay_ms is not modified when NULL is passed

    
}

static void test_next_delay_max(void) {
    falafel_reconnect_t reconnect;
    int result = falafel_reconnect_init(&reconnect, 3000, 10000);
    assert(result == 0);

    uint32_t delay_ms;
    for (uint32_t i = 0; i < 10; i++) {
        result = falafel_reconnect_next_delay(&reconnect, &delay_ms);
        //printf("Attempt %u: Delay = %u ms\n", i + 1, delay_ms);
        assert(result == 0);
        if (i < 2) {
            assert(delay_ms == (3000U << i)); // Exponential growth
        } else {
            assert(delay_ms == 10000); // Should cap at max_delay_ms
        }
    }
}
static void test_next_delay_overflow(void)

{

    falafel_reconnect_t reconnect;
    int result = falafel_reconnect_init(
        &reconnect,
        3000000000U,
        4000000000U
    );
    assert(result == 0);
    uint32_t delay_ms = 0U;
    /* Prima chiamata */
    result = falafel_reconnect_next_delay(&reconnect, &delay_ms);
    assert(result == 0);
    assert(delay_ms == 3000000000U);
    assert(reconnect.current_delay_ms == 4000000000U);
    assert(reconnect.attempts == 1U);
    /* Seconda chiamata */
    result = falafel_reconnect_next_delay(&reconnect, &delay_ms);
    assert(result == 0);
    assert(delay_ms == 4000000000U);
    assert(reconnect.current_delay_ms == 4000000000U);
    assert(reconnect.attempts == 2U);
    /* Terza chiamata */
    result = falafel_reconnect_next_delay(&reconnect, &delay_ms);
    assert(result == 0);
    assert(delay_ms == 4000000000U);
    assert(reconnect.current_delay_ms == 4000000000U);
    assert(reconnect.attempts == 3U);

}
static void test_next_delay_attempts_overflow(void)

{

    falafel_reconnect_t reconnect;
    int result = falafel_reconnect_init(&reconnect, 1000U, 16000U);
    assert(result == 0);
    reconnect.attempts = UINT32_MAX - 1U;
    uint32_t delay_ms = 0U;
    /* Il contatore raggiunge il massimo */
    result = falafel_reconnect_next_delay(&reconnect, &delay_ms);
    assert(result == 0);
    assert(delay_ms == 1000U);
    assert(reconnect.attempts == UINT32_MAX);
    assert(reconnect.current_delay_ms == 2000U);
    /* Il contatore rimane al massimo */
    result = falafel_reconnect_next_delay(&reconnect, &delay_ms);
    assert(result == 0);
    assert(delay_ms == 2000U);
    assert(reconnect.attempts == UINT32_MAX);
    assert(reconnect.current_delay_ms == 4000U);
    /* Il backoff continua a funzionare */
    result = falafel_reconnect_next_delay(&reconnect, &delay_ms);
    assert(result == 0);
    assert(delay_ms == 4000U);
    assert(reconnect.attempts == UINT32_MAX);
    assert(reconnect.current_delay_ms == 8000U);

}

static void test_reset(void)
{
    falafel_reconnect_t reconnect;

    int result = falafel_reconnect_init(&reconnect, 1000U, 16000U);
    assert(result == 0);

    uint32_t delay_ms = 0U;

    // Simulate three failed connection attempts
    for (uint32_t i = 0; i < 3U; i++) {
        result = falafel_reconnect_next_delay(&reconnect, &delay_ms);
        assert(result == 0);
    }

    // Verify state before reset
    assert(reconnect.attempts == 3U);
    assert(reconnect.current_delay_ms == 8000U);

    // Reset backoff
    falafel_reconnect_reset(&reconnect);

    // Verify state after reset
    assert(reconnect.attempts == 0U);
    assert(reconnect.current_delay_ms == 1000U);
    assert(reconnect.initial_delay_ms == 1000U);
    assert(reconnect.max_delay_ms == 16000U);

    // Verify next delay starts from initial value
    result = falafel_reconnect_next_delay(&reconnect, &delay_ms);

    assert(result == 0);
    assert(delay_ms == 1000U);
    assert(reconnect.attempts == 1U);
    assert(reconnect.current_delay_ms == 2000U);

    // NULL must be handled safely
    falafel_reconnect_reset(NULL);
}
int main(void) {
    test_init_valid();
    test_init_null();
    test_init_zero_delay();
    test_init_invalid_max();
    test_init_preserves_state_on_error();
    test_next_delay_sequence();
    test_next_delay_null();
    test_next_delay_max();
    test_next_delay_overflow();
    test_next_delay_attempts_overflow();
    test_reset();
    printf("All tests passed!\n");
    return 0;
}