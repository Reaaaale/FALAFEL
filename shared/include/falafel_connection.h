#ifndef FALAFEL_CONNECTION_H
#define FALAFEL_CONNECTION_H

#include <stdint.h>
#include "falafel_reconnect.h"


typedef enum {
    FALAFEL_CONNECTION_DISCONNECTED,
    FALAFEL_CONNECTION_CONNECTING,
    FALAFEL_CONNECTION_CONNECTED,
    FALAFEL_CONNECTION_WAITING_RETRY
} falafel_connection_state_t;

typedef enum {
    FALAFEL_CONNECTION_EVENT_CONNECT,
    FALAFEL_CONNECTION_EVENT_SUCCESS,
    FALAFEL_CONNECTION_EVENT_FAILURE,
    FALAFEL_CONNECTION_EVENT_TIMEOUT,
    FALAFEL_CONNECTION_EVENT_DISCONNECT
} falafel_connection_event_t;

typedef struct {
    falafel_connection_state_t state;
    falafel_reconnect_t reconnect;
    uint32_t retry_delay_ms;
} falafel_connection_t;


int falafel_connection_init(falafel_connection_t *connection, uint32_t initial_delay_ms, uint32_t max_delay_ms);
int falafel_connection_handle_event(falafel_connection_t *connection, falafel_connection_event_t event);

#endif // FALAFEL_CONNECTION_H