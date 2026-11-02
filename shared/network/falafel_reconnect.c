#include "falafel_reconnect.h"
#include <stddef.h>

int falafel_reconnect_init(falafel_reconnect_t *reconnect, uint32_t initial_delay_ms, uint32_t max_delay_ms){
    if (reconnect == NULL || initial_delay_ms == 0 || max_delay_ms < initial_delay_ms)
    {
        return -1; // Invalid parameters
    }
    reconnect->attempts = 0;
    reconnect->initial_delay_ms = initial_delay_ms;
    reconnect->max_delay_ms = max_delay_ms;
    reconnect->current_delay_ms = initial_delay_ms;

    return 0; // Success
}

int falafel_reconnect_next_delay(falafel_reconnect_t *reconnect, uint32_t *delay_ms) {
    if (reconnect == NULL || delay_ms == NULL)
    {
        return -1;
    }
    *delay_ms = reconnect->current_delay_ms;
    
    if (reconnect->attempts < UINT32_MAX) {
        reconnect->attempts++;
        //return 0; // Prevent overflow
    }
   
    
    if (reconnect-> current_delay_ms <= (reconnect->max_delay_ms - reconnect->current_delay_ms)) {
        reconnect->current_delay_ms *= 2;
    } else {
        reconnect->current_delay_ms = reconnect->max_delay_ms;
    }
    return 0;
}

int falafel_reconnect_reset(falafel_reconnect_t *reconnect) {
    if (reconnect == NULL)
    {
        return -1;
    }
    reconnect->attempts = 0;
    reconnect->current_delay_ms = reconnect->initial_delay_ms;
    return 0;
}