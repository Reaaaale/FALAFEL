#ifndef MESSAGE_MANAGER_H
#define MESSAGE_MANAGER_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "message_catalogue/message_catalog.h"
#include "message_state/message_state.h"


#define MESSAGE_MANAGER_CAPACITY 10

typedef struct outgoing_message_t {
    uint64_t id;
    personality_t personality;
    size_t message_index;
    message_context_t context;
    bool in_use;


} outgoing_message_t;

typedef struct message_manager_t {
    outgoing_message_t messages[MESSAGE_MANAGER_CAPACITY]; // Array of outgoing messages
    uint64_t next_id; // Next unique ID for messages
} message_manager_t;

typedef struct {
    uint32_t created;
    uint32_t removed;
    uint32_t sent;
    uint32_t failed;
    uint32_t retries;
} message_stats_t;

//dichiarazione funzioni

int message_manager_init(message_manager_t *manager, uint64_t next_id);
int message_manager_create(message_manager_t *manager, personality_t personality, size_t message_index, uint32_t max_retries, uint64_t *out_id);
int message_manager_remove(message_manager_t *manager, uint64_t id);
int message_manager_handle_event(message_manager_t *manager, uint64_t id, message_event_t event);
int message_manager_find(message_manager_t *manager, uint64_t id, outgoing_message_t **out_message);


#endif // MESSAGE_MANAGER_H