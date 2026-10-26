#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "desktop_transport.h"
#include "falafel_app.h"

typedef struct endpoint endpoint_t;
struct endpoint {
    falafel_app_t app;
    desktop_transport_t transport;
    uint64_t sequence;
    size_t deliveries;
};

static bool next_id(void *data, uint64_t *value)
{
    endpoint_t *endpoint = data;
    *value = ((uint64_t)endpoint->app.config.device_id << 32) |
             ++endpoint->sequence;
    return true;
}

static bool current_time(void *data, uint64_t *value)
{
    (void)data;
    *value = 1;
    return true;
}

static bool send_event(void *data, const falafel_event_t *event)
{
    endpoint_t *endpoint = data;
    return desktop_transport_send(&endpoint->transport, event);
}

static void receive_event(void *data, const falafel_event_t *event)
{
    endpoint_t *endpoint = data;
    falafel_app_result_t result = falafel_app_receive(&endpoint->app, event, NULL);

    if (event->type == FALAFEL_EVENT_TEXT && result == FALAFEL_APP_OK) {
        falafel_event_t acknowledgement = {
            .source_id = endpoint->app.config.device_id,
            .destination_id = event->source_id,
            .id = event->id,
            .timestamp = event->timestamp,
            .type = FALAFEL_EVENT_ACK,
            .ack_phase = FALAFEL_ACK_DEVICE_RECEIVED,
        };
        ++endpoint->deliveries;
        (void)desktop_transport_send(&endpoint->transport, &acknowledgement);
    }
}

static void initialize(endpoint_t *endpoint, uint16_t port,
                       uint32_t own, uint32_t peer)
{
    falafel_app_config_t config = {
        next_id, current_time, send_event, endpoint, own, peer
    };
    assert(falafel_app_init(&endpoint->app, &config) == FALAFEL_APP_OK);
    desktop_transport_init(&endpoint->transport, "127.0.0.1", port,
                           own, peer, receive_event, endpoint);
}

static uint64_t monotonic_ms(void)
{
    struct timespec value;
    clock_gettime(CLOCK_MONOTONIC, &value);
    return (uint64_t)value.tv_sec * 1000U +
           (uint64_t)value.tv_nsec / 1000000U;
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    char *end = NULL;
    unsigned long parsed = strtoul(argv[1], &end, 10);
    if (*argv[1] == '\0' || *end != '\0' || parsed == 0 || parsed > UINT16_MAX)
        return 2;

    endpoint_t first = {0};
    endpoint_t second = {0};
    initialize(&first, (uint16_t)parsed, 1, 2);
    initialize(&second, (uint16_t)parsed, 2, 1);
    assert(falafel_app_create_text(&first.app, "end-to-end", NULL) ==
           FALAFEL_APP_OK);

    uint64_t deadline = monotonic_ms() + 8000U;
    while (monotonic_ms() < deadline) {
        desktop_transport_poll(&first.transport);
        desktop_transport_poll(&second.transport);
        (void)falafel_app_flush(&first.app);
        (void)falafel_app_flush(&second.app);
        if (first.app.queue[0].state == FALAFEL_MESSAGE_DEVICE_RECEIVED &&
            second.deliveries == 1U) break;
        struct timespec pause = {.tv_nsec = 1000000};
        nanosleep(&pause, NULL);
    }

    bool success = first.app.queue_count == 1U &&
                   first.app.queue[0].state == FALAFEL_MESSAGE_DEVICE_RECEIVED &&
                   second.deliveries == 1U && second.app.received_count == 1U;
    desktop_transport_deinit(&first.transport);
    desktop_transport_deinit(&second.transport);
    if (!success) return 1;
    puts("Desktop transport end-to-end test passed.");
    return 0;
}
