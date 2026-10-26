#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "falafel_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FALAFEL_APP_QUEUE_CAPACITY 16U
#define FALAFEL_APP_SEEN_CAPACITY 64U
#define FALAFEL_APP_RECEIVED_CAPACITY 32U

typedef bool (*falafel_app_next_id_fn)(void *user_data, uint64_t *message_id);
typedef bool (*falafel_app_current_time_fn)(void *user_data, uint64_t *timestamp);
typedef bool (*falafel_app_send_fn)(void *user_data, const falafel_event_t *event);

typedef enum {
    FALAFEL_MESSAGE_CREATED = 0,
    FALAFEL_MESSAGE_QUEUED,
    FALAFEL_MESSAGE_RELAY_ACCEPTED,
    FALAFEL_MESSAGE_DEVICE_RECEIVED,
    FALAFEL_MESSAGE_READ
} falafel_message_state_t;

typedef enum {
    FALAFEL_APP_OK = 0,
    FALAFEL_APP_INVALID_ARGUMENT,
    FALAFEL_APP_EMPTY_TEXT,
    FALAFEL_APP_TEXT_TOO_LONG,
    FALAFEL_APP_PROVIDER_FAILED,
    FALAFEL_APP_QUEUE_FULL,
    FALAFEL_APP_NOT_FOUND,
    FALAFEL_APP_INVALID_TRANSITION,
    FALAFEL_APP_DUPLICATE,
    FALAFEL_APP_IGNORED
} falafel_app_result_t;

typedef struct {
    falafel_app_next_id_fn next_message_id;
    falafel_app_current_time_fn current_time;
    falafel_app_send_fn send_event;
    void *user_data;
    uint32_t device_id;
    uint32_t peer_id;
} falafel_app_config_t;

typedef struct {
    falafel_event_t event;
    falafel_message_state_t state;
    bool submitted;
} falafel_queue_item_t;

typedef struct {
    falafel_app_config_t config;
    falafel_queue_item_t queue[FALAFEL_APP_QUEUE_CAPACITY];
    size_t queue_count;
    uint8_t personality;
    uint64_t personality_revision;
    uint32_t personality_author;
    uint64_t seen_ids[FALAFEL_APP_SEEN_CAPACITY];
    uint32_t seen_sources[FALAFEL_APP_SEEN_CAPACITY];
    size_t seen_count;
    falafel_event_t received[FALAFEL_APP_RECEIVED_CAPACITY];
    size_t received_count;
} falafel_app_t;

falafel_app_result_t falafel_app_init(falafel_app_t *app,
                                      const falafel_app_config_t *config);
falafel_app_result_t falafel_app_create_text(falafel_app_t *app,
                                             const char *text,
                                             uint64_t *message_id);
falafel_app_result_t falafel_app_set_personality(falafel_app_t *app,
                                                 uint8_t personality);
falafel_app_result_t falafel_app_receive(falafel_app_t *app,
                                         const falafel_event_t *event,
                                         bool *changed);
falafel_app_result_t falafel_app_flush(falafel_app_t *app);
falafel_app_result_t falafel_app_transition(falafel_app_t *app,
                                            uint64_t message_id,
                                            falafel_message_state_t next);
const falafel_queue_item_t *falafel_app_queue_at(const falafel_app_t *app,
                                                 size_t index);
void falafel_app_remove_delivered(falafel_app_t *app);

#ifdef __cplusplus
}
#endif
