#ifndef MESSAGE_STATE_H
#define MESSAGE_STATE_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    MESSAGE_STATE_IDLE,
    MESSAGE_STATE_QUEUED,
    MESSAGE_STATE_RELAY_ACCEPTED,
    MESSAGE_STATE_DEVICE_RECEIVED,
    MESSAGE_STATE_FAILED
} message_state_t;

typedef enum {
    MESSAGE_EVENT_QUEUE,
    MESSAGE_EVENT_RELAY_ACK,
    MESSAGE_EVENT_DEVICE_ACK,
    MESSAGE_EVENT_FAILURE,
    MESSAGE_EVENT_RETRY
} message_event_t;

typedef struct {
    message_state_t state;
    uint32_t retry_count;
    uint32_t max_retries;
} message_context_t;

int message_state_init(message_context_t *context, uint32_t max_retries);
int message_state_handle_event(message_context_t *context, message_event_t event);




#endif // MESSAGE_STATE_H