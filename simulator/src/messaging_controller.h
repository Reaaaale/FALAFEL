#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Platform boundary: no LVGL or mailbox ownership escapes this interface.
 * One GUI producer/consumer and one worker. Calls below never wait for I/O.
 * A FreeRTOS implementation can replace the pthread implementation. */
#define MESSAGING_TEXT_MAX 117u
#define MESSAGING_COMMAND_CAPACITY 8u
#define MESSAGING_EVENT_CAPACITY 32u
typedef struct messaging_controller messaging_controller;
typedef struct {
    const char *host, *identity_path, *mailbox_path, *history_path;
    bool create_history; /* explicit v1 archive initialization/migration; never resets v2 */
    uint16_t port;
    uint32_t own, peer;
} messaging_config;
/* STORED confirms a durable application intent; delivery 1 means active outbox,
 * delivery 2 authenticated acceptance. HISTORY restores/upserts without banners. */
typedef enum {
    MESSAGING_READY, MESSAGING_CONNECTION, MESSAGING_STORED,
    MESSAGING_RECEIVED, MESSAGING_DELIVERED, MESSAGING_ERROR, MESSAGING_HISTORY
} messaging_event_kind;
typedef enum {
    MESSAGING_IDENTITY_ERROR = 1, MESSAGING_STORAGE_ERROR,
    MESSAGING_FULL, MESSAGING_AUTH_ERROR, MESSAGING_INTERNAL_ERROR
} messaging_error;
typedef struct {
    messaging_event_kind kind;
    uint64_t request;
    uint8_t id[16];
    char text[MESSAGING_TEXT_MAX + 1];
    bool connected, restored, fatal;
    bool sent, read;
    uint8_t delivery;
    messaging_error error;
} messaging_event;

/* Starts asynchronous initialization. Files must already exist. Identity uses
 * the explicit desktop-mailbox fixture format: private key + peer key (64B).
 * This is a trusted-file adapter, not production provisioning or SAS pairing. */
messaging_controller *messaging_start(const messaging_config *config);
/* True means command accepted in volatile RAM, NEVER durable or delivered.
 * False also means not initialized, closing, invalid text or queue full. */
bool messaging_try_send(messaging_controller *, const char *, size_t, uint64_t *request);
/* Marks locally displayed incoming history as read; never a network receipt. */
bool messaging_try_mark_read(messaging_controller *);
bool messaging_try_event(messaging_controller *, messaging_event *);
void messaging_request_stop(messaging_controller *);
bool messaging_finished(const messaging_controller *);
/* Terminal failure remains observable even if shutdown drops queued events. */
bool messaging_failed(const messaging_controller *);
size_t messaging_static_memory(void);
/* Only after finished, outside the rendering loop. Joins and erases buffers. */
void messaging_destroy(messaging_controller *);
bool messaging_valid_text(const char *, size_t);
