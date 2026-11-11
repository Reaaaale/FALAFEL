#include "message_state.h"
#include <stddef.h>
// inizializza il contesto del messaggio con lo stato iniziale, il conteggio dei tentativi e il numero massimo di tentativi
int message_state_init(message_context_t *context, uint32_t max_retries) {
    if (context == NULL) {
        return -1; // Invalid parameter
    }
    //int result = 0;
    context->state = MESSAGE_STATE_IDLE;
    context->retry_count = 0;
    context->max_retries = max_retries;


 

    return 0; // Success
}

// gestisce gli eventi del messaggio e aggiorna lo stato e il conteggio dei tentativi di conseguenza

int message_state_handle_event(message_context_t *context, message_event_t event) {
    if (context == NULL) {
        return -1; // Invalid parameter
    }

    switch (context->state) {
        case MESSAGE_STATE_IDLE:
            if (event == MESSAGE_EVENT_QUEUE) {
                context->state = MESSAGE_STATE_QUEUED;
            } else {
                return -1; // Invalid event for this state
            }
            break; //idle->queue->queued

        case MESSAGE_STATE_QUEUED:
            if (event == MESSAGE_EVENT_RELAY_ACK) {
                context->state = MESSAGE_STATE_RELAY_ACCEPTED;
            } else if (event == MESSAGE_EVENT_FAILURE) {
                context->state = MESSAGE_STATE_FAILED;
            }
            else if (event == MESSAGE_EVENT_DEVICE_ACK){
                context->state = MESSAGE_STATE_DEVICE_RECEIVED;
            } 
            else {
                return -1; // 
            }
            break;

        case MESSAGE_STATE_RELAY_ACCEPTED:
            if (event == MESSAGE_EVENT_DEVICE_ACK) {
                context->state = MESSAGE_STATE_DEVICE_RECEIVED;
            }  
            else if (event == MESSAGE_EVENT_RELAY_ACK) {
                return 0; // Ignore duplicate relay ack
            }
            else {
                return -1; // Invalid event for this state
            }
            break;

        case MESSAGE_STATE_DEVICE_RECEIVED:

            if (event == MESSAGE_EVENT_RELAY_ACK) {
                return 0;
            }
            else if (event == MESSAGE_EVENT_DEVICE_ACK){
                return 0; // Ignore duplicate device ack    
            }

            return -1; // Invalid event for this state

        case MESSAGE_STATE_FAILED:
            if (event == MESSAGE_EVENT_RETRY && context->retry_count < context->max_retries) {
                context->retry_count++;
                context->state = MESSAGE_STATE_QUEUED; // Retry the message
            } 
            else if (event == MESSAGE_EVENT_RELAY_ACK) {
                context->state = MESSAGE_STATE_RELAY_ACCEPTED;
            }
            else if (event == MESSAGE_EVENT_DEVICE_ACK) {
                 context->state = MESSAGE_STATE_DEVICE_RECEIVED;
            }
           
            else {
                return -1; // Invalid event for this state or max retries reached
            }
            break;

        default:
            return -1; // Unknown state
    }

    return 0; // Success
    
}

