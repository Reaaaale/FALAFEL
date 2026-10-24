#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "falafel_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*falafel_app_next_id_fn)(
    void *user_data,
    uint64_t *message_id
);

typedef bool (*falafel_app_current_time_fn)(
    void *user_data,
    uint64_t *timestamp
);

typedef struct {
    falafel_app_next_id_fn next_message_id;
    falafel_app_current_time_fn current_time;
    void *user_data;
} falafel_app_config_t;

typedef struct {
    falafel_app_config_t config;
} falafel_app_t;

typedef enum {
    FALAFEL_APP_OK = 0,
    FALAFEL_APP_INVALID_ARGUMENT
} falafel_app_result_t;

/**
 * Initialize an application context.
 *
 * The configuration is copied into the context.
 */
falafel_app_result_t falafel_app_init(
    falafel_app_t *app,
    const falafel_app_config_t *config
);

#ifdef __cplusplus
}
#endif
