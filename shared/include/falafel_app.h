#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "falafel_protocol.h"
#ifdef __cplusplus
extern "C" {
#endif
#define FALAFEL_APP_QUEUE_CAPACITY 16U
#define FALAFEL_APP_SEEN_CAPACITY 32U
typedef bool (*falafel_app_next_id_fn)(void *, uint64_t *);
typedef bool (*falafel_app_current_time_fn)(void *, uint64_t *);
typedef bool (*falafel_app_send_fn)(void *, const falafel_event_t *);
typedef enum { FALAFEL_MESSAGE_CREATED=0, FALAFEL_MESSAGE_QUEUED,
    FALAFEL_MESSAGE_RELAY_ACCEPTED, FALAFEL_MESSAGE_DEVICE_RECEIVED,
    FALAFEL_MESSAGE_READ } falafel_message_state_t;
typedef enum { FALAFEL_APP_OK=0, FALAFEL_APP_INVALID_ARGUMENT,
    FALAFEL_APP_EMPTY_TEXT, FALAFEL_APP_TEXT_TOO_LONG, FALAFEL_APP_PROVIDER_FAILED,
    FALAFEL_APP_QUEUE_FULL, FALAFEL_APP_NOT_FOUND, FALAFEL_APP_INVALID_TRANSITION,
    FALAFEL_APP_DUPLICATE, FALAFEL_APP_IGNORED } falafel_app_result_t;
typedef struct { falafel_app_next_id_fn next_message_id; falafel_app_current_time_fn current_time;
    falafel_app_send_fn send_event; void *user_data; uint32_t device_id, peer_id; } falafel_app_config_t;
typedef struct { falafel_event_t event; falafel_message_state_t state; } falafel_queue_item_t;
typedef struct {
    falafel_app_config_t config;
    falafel_queue_item_t queue[FALAFEL_APP_QUEUE_CAPACITY]; size_t queue_count;
    uint8_t personality; uint64_t personality_revision; uint32_t personality_author;
    uint64_t seen_ids[FALAFEL_APP_SEEN_CAPACITY]; uint32_t seen_sources[FALAFEL_APP_SEEN_CAPACITY]; size_t seen_count;
} falafel_app_t;
falafel_app_result_t falafel_app_init(falafel_app_t *, const falafel_app_config_t *);
falafel_app_result_t falafel_app_create_text(falafel_app_t *, const char *, uint64_t *);
falafel_app_result_t falafel_app_set_personality(falafel_app_t *, uint8_t);
falafel_app_result_t falafel_app_receive(falafel_app_t *, const falafel_event_t *, bool *);
falafel_app_result_t falafel_app_flush(falafel_app_t *);
falafel_app_result_t falafel_app_transition(falafel_app_t *, uint64_t, falafel_message_state_t);
const falafel_queue_item_t *falafel_app_queue_at(const falafel_app_t *, size_t);
void falafel_app_remove_delivered(falafel_app_t *);
#ifdef __cplusplus
}
#endif
