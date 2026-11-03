#include <stdio.h>

typedef enum {
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_ERROR
} wifi_state_t;

int wifi_is_transition_valid (wifi_state_t current, wifi_state_t next){
    switch (current) {
        case WIFI_STATE_DISCONNECTED:
            return (next == WIFI_STATE_CONNECTING);
        case WIFI_STATE_CONNECTING:
            return (next == WIFI_STATE_CONNECTED || next == WIFI_STATE_ERROR || next == WIFI_STATE_DISCONNECTED);
        case WIFI_STATE_CONNECTED:
            return (next == WIFI_STATE_DISCONNECTED || next == WIFI_STATE_ERROR);
        case WIFI_STATE_ERROR:
            return (next == WIFI_STATE_DISCONNECTED || next == WIFI_STATE_CONNECTING);
        default:
            return 0;
    }
}
void wifi_set_state (wifi_state_t *state, wifi_state_t new_state) {
    if (state == NULL) {
        printf("Error: state pointer is NULL\n");
        return;

    }
    if (*state == new_state) {
        //printf("State is already %d\n", new_state);
        return;
    }
    //*state = new_state;

    int is_transition_valid = wifi_is_transition_valid(*state, new_state);
    if (!is_transition_valid) {
        printf("Error: Invalid transition from state %d to state %d\n", *state, new_state);
        return;
    }

    switch (new_state) {
        case WIFI_STATE_DISCONNECTED:
            printf("WiFi: Disconnected\n");
            break;
        case WIFI_STATE_CONNECTING:
            printf("WiFi: Connecting\n");
            break;
        case WIFI_STATE_CONNECTED:
            printf("WiFi: Connected\n");
            break;
        case WIFI_STATE_ERROR:
            printf("WiFi: Error\n");
            break;
        default:
            printf("Error: Invalid WiFi state %d\n", new_state);
            return;
    }
    *state = new_state;
}
    /*
    if (is_transition_valid) {
        *state = new_state;
    } else {
        printf("Error: Invalid transition from state %d to state %d\n", *state, new_state);
    }
}*/



int main(void)
{
    wifi_state_t state = WIFI_STATE_DISCONNECTED;

    wifi_set_state(&state, WIFI_STATE_CONNECTING);
    wifi_set_state(&state, WIFI_STATE_CONNECTED);
    wifi_set_state(&state, WIFI_STATE_CONNECTED);
    wifi_set_state(&state, WIFI_STATE_DISCONNECTED);
    wifi_set_state(&state, WIFI_STATE_ERROR);
    wifi_set_state(&state, (wifi_state_t)99);

    printf("Final state: %d\n", (int)state);

    return 0;
}