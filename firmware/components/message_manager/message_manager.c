#include "message_manager.h"
# include <stdbool.h>
#include <stdint.h>
#include "message_catalogue/message_catalog.h"
#include "message_state/message_state.h"


int message_manager_init (message_manager_t *manager, uint64_t next_id) {
    if (manager == NULL) {
        return -1; // Error: Null pointer
    }

    manager->next_id = next_id;

    for (size_t i = 0; i < MESSAGE_MANAGER_CAPACITY; ++i) {
        manager->messages[i].in_use = false; // Mark all messages as not in use
    }

    return 0; // Success
}


int message_manager_create(message_manager_t *manager, personality_t personality, size_t message_index, uint32_t max_retries, uint64_t *out_id) {
    if (manager == NULL || out_id == NULL) {
        return -1; // Error: Null pointer
    }
    // devo utilizzare message_state_init per inizializzare il contesto del messaggio con lo stato iniziale, il conteggio dei tentativi e il numero massimo di tentativi

    for (size_t i = 0; i < MESSAGE_MANAGER_CAPACITY; ++i) {
        if (!manager->messages[i].in_use) {

            // Found an available slot
            if (manager->next_id == UINT64_MAX) {
                return -1; // Error: ID overflow
            }
            manager->messages[i].id = manager->next_id;
            
            manager->messages[i].personality = personality;
            manager->messages[i].message_index = message_index;
            if (message_state_init(&manager->messages[i].context, max_retries) != 0) {
                return -1; // Error initializing message context
            }
            manager->messages[i].in_use = true;

            *out_id = manager->messages[i].id; // Return the new message ID
            manager->next_id++; // Increment the next ID for future messages
            return 0; // Success
        }
    }

    return -1;// Error: No available slots
}

int message_manager_remove(message_manager_t *manager, uint64_t id) {
    if (manager == NULL) {
        return -1; // Error: Null pointer
    }

    for (size_t i = 0; i < MESSAGE_MANAGER_CAPACITY; ++i) {
        if (manager->messages[i].in_use && manager->messages[i].id == id) {
            manager->messages[i].in_use = false; // Mark the message as not in use
            return 0; // Success
        }
    }

    return -1; // Error: Message not found
}

int message_manager_find (message_manager_t *manager, uint64_t id, outgoing_message_t **out_message) {
    if (manager == NULL || out_message == NULL) {
        return -1; // Error: Null pointer
    }

    for (size_t i = 0; i < MESSAGE_MANAGER_CAPACITY; ++i) {
        if (manager->messages[i].in_use && manager->messages[i].id == id) {
            *out_message = &manager->messages[i]; // Return the found message
            return 0; // Success
        }
    }

    return -1; // Error: Message not found
}


int message_manager_handle_event(message_manager_t *manager,
                                 uint64_t id,
                                 message_event_t event) {
    outgoing_message_t *message = NULL;

    int result = message_manager_find(manager, id, &message);
    if (result != 0) {
        return result;
    }

    return message_state_handle_event(&message->context, event);
}