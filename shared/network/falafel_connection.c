#include "falafel_connection.h"
#include <stddef.h>

int falafel_connection_init (falafel_connection_t *connection,
uint32_t initial_delay_ms, uint32_t max_delay_ms) {
    if (connection == NULL)
        return -1;

    // inizializzazione modulo falafel_reconnect utilizzando i parametri ricevuti
    int result = falafel_reconnect_init(&connection->reconnect, initial_delay_ms, max_delay_ms);
    //imposto stato iniziale della connessione a DISCONNECTED
    //falafel_connection_state_t initial_state = FALAFEL_CONNECTION_DISCONNECTED;
    if (result != 0) {
        return -1; // Initialization failed
    }
    connection->state = FALAFEL_CONNECTION_DISCONNECTED;
    connection->retry_delay_ms = 0; // Imposta il ritardo di retry a zerro all'inizio
    
    return 0;


}

int falafel_connection_handle_event(falafel_connection_t *connection, falafel_connection_event_t event) {
    if (connection == NULL) {
        return -1; // Invalid parameter
    }

    switch (connection->state) {
        case FALAFEL_CONNECTION_DISCONNECTED:
            if (event == FALAFEL_CONNECTION_EVENT_CONNECT) {
                connection->state = FALAFEL_CONNECTION_CONNECTING;
            } else {
                return -1; // Invalid event for this state
            }
            break;

        case FALAFEL_CONNECTION_CONNECTING:
            if (event == FALAFEL_CONNECTION_EVENT_SUCCESS) {
                //check reset is successful
                int result = falafel_reconnect_reset(&connection->reconnect);
                if (result != 0) {
                    return -1; // Handle error if needed
                }
                connection->state = FALAFEL_CONNECTION_CONNECTED;
                
                connection->retry_delay_ms = 0; // Reset retry delay on successful connection
            } else if (event == FALAFEL_CONNECTION_EVENT_FAILURE) {
                
                uint32_t delay_ms;
                

                //check result function return value
                int result = falafel_reconnect_next_delay(&connection->reconnect, &delay_ms);
                if (result != 0) {
                    return -1; // Handle error if needed
                }
                connection->state = FALAFEL_CONNECTION_WAITING_RETRY;
                connection->retry_delay_ms = delay_ms; // Set the retry delay
            }
            else {
                return -1; // Invalid event for this state
            }
            break;

        case FALAFEL_CONNECTION_CONNECTED:
            
            if (event == FALAFEL_CONNECTION_EVENT_DISCONNECT) {
                int result = falafel_reconnect_reset(&connection->reconnect); // Reset the reconnect state on successful connection
                if (result != 0) {
                    return -1; // Handle error if needed
                }
                connection->state = FALAFEL_CONNECTION_DISCONNECTED;
                connection->retry_delay_ms = 0; // Reset retry delay on disconnect
            }
            else {
                return -1; // Invalid event for this state
            }
            break;

        case FALAFEL_CONNECTION_WAITING_RETRY:
            if (event == FALAFEL_CONNECTION_EVENT_TIMEOUT) {
                connection->state = FALAFEL_CONNECTION_CONNECTING;
                connection->retry_delay_ms = 0; // Reset retry delay on timeout


            }
            else {
                return -1; // Invalid event for this state
            }
            break;

        default:
            return -1; // Invalid state
    }

    return 0; // Event handled successfully
}

