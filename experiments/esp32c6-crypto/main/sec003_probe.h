#pragma once
#include <stddef.h>
#include <stdint.h>

/* Experiment only. No output bytes or private provider layout are inspected. */
typedef struct {
    uint32_t entropy_calls, injected_failures, hardware_calls, hardware_bytes;
} sec003_probe_stats;
void sec003_probe_reset(void);
void sec003_probe_fail_next(void);
void sec003_probe_cancel_failure(void);
sec003_probe_stats sec003_probe_get(void);
int sec003_entropy_func(void *, unsigned char *, size_t);
