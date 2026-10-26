#pragma once

#include <netdb.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>

#include "falafel_app.h"

#define DESKTOP_TRANSPORT_INPUT_CAPACITY 4096U
#define DESKTOP_TRANSPORT_OUTPUT_CAPACITY 1024U

typedef void (*desktop_event_fn)(void *user_data, const falafel_event_t *event);

typedef enum {
    DESKTOP_TRANSPORT_DISCONNECTED = 0,
    DESKTOP_TRANSPORT_RESOLVING,
    DESKTOP_TRANSPORT_CONNECTING,
    DESKTOP_TRANSPORT_HANDSHAKE,
    DESKTOP_TRANSPORT_CONNECTED
} desktop_transport_state_t;

typedef struct {
    int fd;
    char host[128];
    uint16_t port;
    uint32_t device_id;
    uint32_t peer_id;
    uint8_t input[DESKTOP_TRANSPORT_INPUT_CAPACITY];
    size_t input_length;
    uint8_t output[DESKTOP_TRANSPORT_OUTPUT_CAPACITY];
    size_t output_offset;
    size_t output_length;
    uint64_t reconnect_after_ms;
    uint64_t connect_deadline_ms;
    desktop_event_fn on_event;
    void *user_data;
    desktop_transport_state_t state;
    pthread_t resolver_thread;
    pthread_mutex_t resolver_mutex;
    bool resolver_started;
    bool resolver_done;
    int resolver_error;
    struct sockaddr_storage resolved_address;
    socklen_t resolved_length;
} desktop_transport_t;

typedef enum {
    DESKTOP_STATE_OK = 0,
    DESKTOP_STATE_NOT_FOUND,
    DESKTOP_STATE_CORRUPT,
    DESKTOP_STATE_UNSUPPORTED,
    DESKTOP_STATE_IO_ERROR
} desktop_state_result_t;

void desktop_transport_init(desktop_transport_t *transport, const char *host,
                            uint16_t port, uint32_t device_id,
                            uint32_t peer_id, desktop_event_fn callback,
                            void *user_data);
void desktop_transport_deinit(desktop_transport_t *transport);
void desktop_transport_poll(desktop_transport_t *transport);
bool desktop_transport_send(void *context, const falafel_event_t *event);
bool desktop_transport_is_connected(const desktop_transport_t *transport);
void desktop_transport_force_reconnect(desktop_transport_t *transport);

desktop_state_result_t desktop_state_load(const char *path, falafel_app_t *app,
                                          uint32_t *reserved_sequence);
bool desktop_state_save(const char *path, const falafel_app_t *app,
                        uint32_t reserved_sequence);
bool desktop_allocate_event_id(const char *path, const falafel_app_t *app,
                               uint32_t device_id, uint32_t *next_sequence,
                               uint32_t *reserved_sequence,
                               uint64_t *event_id);
