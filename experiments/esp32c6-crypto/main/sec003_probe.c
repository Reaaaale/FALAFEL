#include "sec003_probe.h"
#include "mbedtls/entropy.h"
#include <stdbool.h>

static sec003_probe_stats stats;
static bool fail_next;
void sec003_probe_reset(void) { stats = (sec003_probe_stats){0}; fail_next = false; }
void sec003_probe_fail_next(void) { fail_next = true; }
void sec003_probe_cancel_failure(void) { fail_next = false; }
sec003_probe_stats sec003_probe_get(void) { return stats; }

/* Only the experiment's provider object names this proxy as its callback.
 * Positive calls delegate unchanged to the real Mbed TLS entropy accumulator.
 * Injection is an API error, NOT a measurement of absent/weak physical entropy. */
int sec003_entropy_func(void *context, unsigned char *out, size_t n)
{
    ++stats.entropy_calls;
    if (fail_next) {
        fail_next = false;
        ++stats.injected_failures;
        return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    }
    return mbedtls_entropy_func(context, out, n);
}

#ifdef ESP_PLATFORM
/* GNU linker interposition only in this experiment. The SDK's original
 * hardware poll still obtains all positive samples through esp_fill_random. */
int __real_mbedtls_hardware_poll(void *, unsigned char *, size_t, size_t *);
int __wrap_mbedtls_hardware_poll(void *context, unsigned char *out, size_t n, size_t *written)
{
    ++stats.hardware_calls;
    int result = __real_mbedtls_hardware_poll(context, out, n, written);
    if (!result) stats.hardware_bytes += (uint32_t)*written;
    return result;
}
#endif
