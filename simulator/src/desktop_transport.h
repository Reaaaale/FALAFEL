#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "falafel_app.h"
typedef void (*desktop_event_fn)(void *, const falafel_event_t *);
typedef struct {
    int fd; char host[128]; uint16_t port; uint32_t device_id, peer_id;
    uint8_t input[4096]; size_t input_length; uint64_t reconnect_after_ms;
    desktop_event_fn on_event; void *user_data;
} desktop_transport_t;
void desktop_transport_init(desktop_transport_t *,const char *,uint16_t,uint32_t,uint32_t,desktop_event_fn,void *);
void desktop_transport_poll(desktop_transport_t *);
bool desktop_transport_send(void *,const falafel_event_t *);
bool desktop_state_load(const char *,falafel_app_t *);
bool desktop_state_save(const char *,const falafel_app_t *);
