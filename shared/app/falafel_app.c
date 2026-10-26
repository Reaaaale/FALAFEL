#include "falafel_app.h"

#include <string.h>

static size_t bounded_length(const char *text, size_t limit)
{
    size_t length = 0;

    while (length <= limit && text[length] != '\0') {
        ++length;
    }
    return length;
}

static falafel_app_result_t enqueue(falafel_app_t *app,
                                    const falafel_event_t *event)
{
    if (app->queue_count == FALAFEL_APP_QUEUE_CAPACITY) {
        return FALAFEL_APP_QUEUE_FULL;
    }

    falafel_queue_item_t item = {
        .event = *event,
        .state = FALAFEL_MESSAGE_QUEUED,
    };
    app->queue[app->queue_count++] = item;
    return FALAFEL_APP_OK;
}

static falafel_app_result_t create_event(falafel_app_t *app,
                                         falafel_event_t *event)
{
    if (!app->config.next_message_id(app->config.user_data, &event->id) ||
        event->id == 0U ||
        !app->config.current_time(app->config.user_data, &event->timestamp)) {
        return FALAFEL_APP_PROVIDER_FAILED;
    }
    return FALAFEL_APP_OK;
}

falafel_app_result_t falafel_app_init(falafel_app_t *app,
                                      const falafel_app_config_t *config)
{
    if (app == NULL || config == NULL || config->next_message_id == NULL ||
        config->current_time == NULL || config->device_id == 0U ||
        config->peer_id == 0U || config->device_id == config->peer_id) {
        return FALAFEL_APP_INVALID_ARGUMENT;
    }

    falafel_app_t initialized = {0};
    initialized.config = *config;
    initialized.personality_author = config->device_id;
    *app = initialized;
    return FALAFEL_APP_OK;
}

falafel_app_result_t falafel_app_create_text(falafel_app_t *app,
                                             const char *text,
                                             uint64_t *message_id)
{
    falafel_event_t event = {0};
    size_t length;
    falafel_app_result_t result;

    if (app == NULL || text == NULL) {
        return FALAFEL_APP_INVALID_ARGUMENT;
    }
    length = bounded_length(text, FALAFEL_MAX_PAYLOAD_SIZE);
    if (length == 0U) {
        return FALAFEL_APP_EMPTY_TEXT;
    }
    if (length > FALAFEL_MAX_PAYLOAD_SIZE) {
        return FALAFEL_APP_TEXT_TOO_LONG;
    }
    if (app->queue_count == FALAFEL_APP_QUEUE_CAPACITY) {
        return FALAFEL_APP_QUEUE_FULL;
    }

    event.source_id = app->config.device_id;
    event.destination_id = app->config.peer_id;
    event.type = FALAFEL_EVENT_TEXT;
    event.payload_length = (uint16_t)length;
    result = create_event(app, &event);
    if (result != FALAFEL_APP_OK) {
        return result;
    }
    memcpy(event.payload, text, length);

    result = enqueue(app, &event);
    if (result == FALAFEL_APP_OK && message_id != NULL) {
        *message_id = event.id;
    }
    return result;
}

static falafel_queue_item_t *find_unsent_personality(falafel_app_t *app)
{
    for (size_t index = app->queue_count; index > 0U; --index) {
        falafel_queue_item_t *item = &app->queue[index - 1U];
        if (item->event.type == FALAFEL_EVENT_PERSONALITY &&
            item->state == FALAFEL_MESSAGE_QUEUED && !item->submitted) {
            return item;
        }
    }
    return NULL;
}

falafel_app_result_t falafel_app_set_personality(falafel_app_t *app,
                                                 uint8_t personality)
{
    falafel_event_t event = {0};
    falafel_queue_item_t *replace;
    falafel_app_result_t result;

    if (app == NULL || personality >= 3U) {
        return FALAFEL_APP_INVALID_ARGUMENT;
    }
    replace = find_unsent_personality(app);
    if (replace == NULL && app->queue_count == FALAFEL_APP_QUEUE_CAPACITY) {
        return FALAFEL_APP_QUEUE_FULL;
    }

    event.source_id = app->config.device_id;
    event.destination_id = app->config.peer_id;
    event.type = FALAFEL_EVENT_PERSONALITY;
    event.personality = personality;
    event.revision = app->personality_revision + 1U;
    if (event.revision == 0U) {
        return FALAFEL_APP_INVALID_TRANSITION;
    }
    result = create_event(app, &event);
    if (result != FALAFEL_APP_OK) {
        return result;
    }

    if (replace != NULL) {
        replace->event = event;
    } else {
        result = enqueue(app, &event);
        if (result != FALAFEL_APP_OK) {
            return result;
        }
    }
    app->personality = personality;
    app->personality_revision = event.revision;
    app->personality_author = app->config.device_id;
    return FALAFEL_APP_OK;
}

static bool event_was_seen(const falafel_app_t *app,
                           const falafel_event_t *event)
{
    for (size_t index = 0; index < app->seen_count; ++index) {
        if (app->seen_ids[index] == event->id &&
            app->seen_sources[index] == event->source_id) {
            return true;
        }
    }
    return false;
}

static void remember_event(falafel_app_t *app, const falafel_event_t *event)
{
    size_t index;

    if (app->seen_count < FALAFEL_APP_SEEN_CAPACITY) {
        index = app->seen_count++;
    } else {
        index = (size_t)(event->id % FALAFEL_APP_SEEN_CAPACITY);
    }
    app->seen_ids[index] = event->id;
    app->seen_sources[index] = event->source_id;
}

falafel_app_result_t falafel_app_transition(falafel_app_t *app,
                                            uint64_t message_id,
                                            falafel_message_state_t next)
{
    if (app == NULL) {
        return FALAFEL_APP_INVALID_ARGUMENT;
    }

    for (size_t index = 0; index < app->queue_count; ++index) {
        falafel_queue_item_t *item = &app->queue[index];
        falafel_message_state_t current;

        if (item->event.id != message_id) {
            continue;
        }
        current = item->state;
        if (next == current) {
            return FALAFEL_APP_OK;
        }
        if ((current == FALAFEL_MESSAGE_QUEUED &&
             next != FALAFEL_MESSAGE_RELAY_ACCEPTED) ||
            (current == FALAFEL_MESSAGE_RELAY_ACCEPTED &&
             next != FALAFEL_MESSAGE_DEVICE_RECEIVED) ||
            (current == FALAFEL_MESSAGE_DEVICE_RECEIVED &&
             next != FALAFEL_MESSAGE_READ) ||
            next <= current || next > FALAFEL_MESSAGE_READ) {
            return FALAFEL_APP_INVALID_TRANSITION;
        }
        item->state = next;
        return FALAFEL_APP_OK;
    }
    return FALAFEL_APP_NOT_FOUND;
}

static bool valid_origin(const falafel_app_t *app,
                         const falafel_event_t *event)
{
    if (event->destination_id != app->config.device_id) {
        return false;
    }
    if (event->type == FALAFEL_EVENT_ACK) {
        if (event->ack_phase == FALAFEL_ACK_RELAY_ACCEPTED) {
            return event->source_id == FALAFEL_RELAY_ID;
        }
        return event->ack_phase == FALAFEL_ACK_DEVICE_RECEIVED &&
               (event->source_id == app->config.peer_id ||
                event->source_id == FALAFEL_RELAY_ID);
    }
    return (event->type == FALAFEL_EVENT_TEXT ||
            event->type == FALAFEL_EVENT_PERSONALITY) &&
           event->source_id == app->config.peer_id;
}

falafel_app_result_t falafel_app_receive(falafel_app_t *app,
                                         const falafel_event_t *event,
                                         bool *changed)
{
    if (app == NULL || event == NULL || !falafel_event_is_valid(event) ||
        !valid_origin(app, event)) {
        return FALAFEL_APP_INVALID_ARGUMENT;
    }
    if (changed != NULL) {
        *changed = false;
    }

    if (event->type == FALAFEL_EVENT_ACK) {
        falafel_message_state_t state =
            event->ack_phase == FALAFEL_ACK_RELAY_ACCEPTED
                ? FALAFEL_MESSAGE_RELAY_ACCEPTED
                : FALAFEL_MESSAGE_DEVICE_RECEIVED;
        return falafel_app_transition(app, event->id, state);
    }
    if (event_was_seen(app, event)) {
        return FALAFEL_APP_DUPLICATE;
    }
    if (event->type == FALAFEL_EVENT_TEXT &&
        app->received_count == FALAFEL_APP_RECEIVED_CAPACITY) {
        return FALAFEL_APP_QUEUE_FULL;
    }
    remember_event(app, event);

    if (event->type == FALAFEL_EVENT_TEXT) {
        app->received[app->received_count++] = *event;
    }

    if (event->type == FALAFEL_EVENT_PERSONALITY) {
        bool newer = event->revision > app->personality_revision ||
                     (event->revision == app->personality_revision &&
                      event->source_id > app->personality_author);
        if (!newer) {
            return FALAFEL_APP_IGNORED;
        }
        app->personality = event->personality;
        app->personality_revision = event->revision;
        app->personality_author = event->source_id;
        if (changed != NULL) {
            *changed = true;
        }
    }
    return FALAFEL_APP_OK;
}

falafel_app_result_t falafel_app_flush(falafel_app_t *app)
{
    if (app == NULL) {
        return FALAFEL_APP_INVALID_ARGUMENT;
    }
    if (app->config.send_event == NULL) {
        return FALAFEL_APP_OK;
    }

    for (size_t index = 0; index < app->queue_count; ++index) {
        if (app->queue[index].state < FALAFEL_MESSAGE_DEVICE_RECEIVED) {
            if (!app->config.send_event(app->config.user_data,
                                        &app->queue[index].event)) {
                return FALAFEL_APP_PROVIDER_FAILED;
            }
            app->queue[index].submitted = true;
        }
    }
    return FALAFEL_APP_OK;
}

const falafel_queue_item_t *falafel_app_queue_at(const falafel_app_t *app,
                                                 size_t index)
{
    return app != NULL && index < app->queue_count ? &app->queue[index] : NULL;
}

void falafel_app_remove_delivered(falafel_app_t *app)
{
    size_t write_index = 0;

    if (app == NULL) {
        return;
    }
    for (size_t read_index = 0; read_index < app->queue_count; ++read_index) {
        if (app->queue[read_index].state < FALAFEL_MESSAGE_DEVICE_RECEIVED) {
            app->queue[write_index++] = app->queue[read_index];
        }
    }
    app->queue_count = write_index;
}
