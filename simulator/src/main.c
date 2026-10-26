#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "SDL2/SDL.h"
#include "desktop_transport.h"
#include "falafel_app.h"
#include "falafel_gui.h"
#include "lvgl.h"
#include "src/drivers/sdl/lv_sdl_mouse.h"
#include "src/drivers/sdl/lv_sdl_window.h"
#include "trackpad_navigation.h"

enum {
    DISPLAY_WIDTH = 320,
    DISPLAY_HEIGHT = 480,
};

typedef struct {
    falafel_app_t app;
    desktop_transport_t transport;
    uint32_t next_sequence;
    uint32_t reserved_sequence;
    char state_path[384];
    uint32_t device_id;
    uint16_t unread;
} simulator_t;

static bool save_state(simulator_t *simulator)
{
    bool saved = desktop_state_save(simulator->state_path, &simulator->app,
                                    simulator->reserved_sequence);
    if (!saved) {
        fprintf(stderr, "[STATE] Unable to save %s\n", simulator->state_path);
    }
    return saved;
}

static bool next_id(void *data, uint64_t *message_id)
{
    simulator_t *simulator = data;
    return desktop_allocate_event_id(
        simulator->state_path, &simulator->app, simulator->device_id,
        &simulator->next_sequence, &simulator->reserved_sequence, message_id);
}

static bool current_time(void *data, uint64_t *value)
{
    time_t now;
    (void)data;
    now = time(NULL);
    if (now == (time_t)-1) {
        return false;
    }
    *value = (uint64_t)now;
    return true;
}

static bool send_event(void *data, const falafel_event_t *event)
{
    simulator_t *simulator = data;
    return desktop_transport_send(&simulator->transport, event);
}

static void send_device_ack(simulator_t *simulator,
                            const falafel_event_t *received)
{
    falafel_event_t acknowledgement = {
        .source_id = simulator->app.config.device_id,
        .destination_id = received->source_id,
        .id = received->id,
        .timestamp = received->timestamp,
        .type = FALAFEL_EVENT_ACK,
        .ack_phase = FALAFEL_ACK_DEVICE_RECEIVED,
    };
    (void)desktop_transport_send(&simulator->transport, &acknowledgement);
}

static void show_received_text(simulator_t *simulator,
                               const falafel_event_t *event)
{
    char text[FALAFEL_MAX_PAYLOAD_SIZE + 1U];
    memcpy(text, event->payload, event->payload_length);
    text[event->payload_length] = '\0';
    falafel_gui_add_received_message(text);
    if (simulator->unread < UINT16_MAX) {
        ++simulator->unread;
    }
    falafel_gui_set_unread_count(simulator->unread);
}

static void incoming(void *data, const falafel_event_t *event)
{
    simulator_t *simulator = data;
    falafel_app_t previous = simulator->app;
    bool changed = false;
    falafel_app_result_t result =
        falafel_app_receive(&simulator->app, event, &changed);

    if (result != FALAFEL_APP_OK && result != FALAFEL_APP_IGNORED &&
        result != FALAFEL_APP_DUPLICATE) {
        return;
    }
    if (!save_state(simulator)) {
        simulator->app = previous;
        return;
    }

    if (event->type == FALAFEL_EVENT_ACK) {
        if (result != FALAFEL_APP_OK) {
            return;
        }
        if (event->ack_phase == FALAFEL_ACK_RELAY_ACCEPTED) {
            puts("[STATE] RELAY_ACCEPTED");
            falafel_gui_show_notification("Relay: accettato");
        } else {
            puts("[STATE] DEVICE_RECEIVED");
            falafel_gui_show_notification("Dispositivo: ricevuto");
            falafel_app_remove_delivered(&simulator->app);
            (void)save_state(simulator);
        }
        return;
    }
    if (result == FALAFEL_APP_OK && event->type == FALAFEL_EVENT_TEXT) {
        show_received_text(simulator, event);
    }
    if (changed) {
        falafel_gui_set_personality_remote(
            (falafel_personality_t)simulator->app.personality);
    }
    send_device_ack(simulator, event);
}

static bool gui_send(const char *text, void *data)
{
    simulator_t *simulator = data;
    falafel_app_t previous = simulator->app;

    if (falafel_app_create_text(&simulator->app, text, NULL) != FALAFEL_APP_OK) {
        return false;
    }
    if (!save_state(simulator)) {
        simulator->app = previous;
        return false;
    }
    return true;
}

static void gui_personality(falafel_personality_t value, void *data)
{
    simulator_t *simulator = data;
    falafel_app_t previous = simulator->app;

    if (falafel_app_set_personality(&simulator->app, (uint8_t)value) !=
            FALAFEL_APP_OK || !save_state(simulator)) {
        simulator->app = previous;
        falafel_gui_show_notification("Cambio non accodato");
    }
}

static void gui_messages(void *data)
{
    simulator_t *simulator = data;
    simulator->unread = 0U;
}

static const char *argument(int argc, char **argv, const char *name,
                            const char *fallback)
{
    for (int index = 1; index + 1 < argc; ++index) {
        if (strcmp(argv[index], name) == 0) {
            return argv[index + 1];
        }
    }
    return fallback;
}

static bool parse_u32(const char *text, uint32_t minimum, uint32_t maximum,
                      uint32_t *value)
{
    char *end = NULL;
    unsigned long parsed;

    if (text == NULL || *text == '\0' || *text == '-') return false;
    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed < minimum ||
        parsed > maximum) return false;
    *value = (uint32_t)parsed;
    return true;
}

int main(int argc, char **argv)
{
    const char *device_text = argument(argc, argv, "--device",
                                       getenv("FALAFEL_DEVICE_ID"));
    const char *peer_text = argument(argc, argv, "--peer",
                                     getenv("FALAFEL_PEER_ID"));
    const char *host = argument(argc, argv, "--host",
                                getenv("FALAFEL_RELAY_HOST"));
    const char *port_text = argument(argc, argv, "--port",
                                     getenv("FALAFEL_RELAY_PORT"));
    const char *state = argument(argc, argv, "--data",
                                 getenv("FALAFEL_DATA_FILE"));
    uint32_t device_id;
    uint32_t peer_id;
    uint32_t port_value = 7777U;
    simulator_t simulator = {0};
    desktop_state_result_t load_result;

    if (!parse_u32(device_text, 1U, UINT32_MAX - 1U, &device_id) ||
        !parse_u32(peer_text, 1U, UINT32_MAX - 1U, &peer_id) ||
        device_id == peer_id ||
        (port_text != NULL &&
         !parse_u32(port_text, 1U, UINT16_MAX, &port_value))) {
        fprintf(stderr, "Usage: %s --device ID --peer ID [--host HOST] "
                        "[--port PORT] [--data FILE]\n", argv[0]);
        return EXIT_FAILURE;
    }
    simulator.device_id = device_id;
    snprintf(simulator.state_path, sizeof(simulator.state_path), "%s",
             state != NULL ? state : "falafel-device.state");
    desktop_transport_init(&simulator.transport,
                           host != NULL ? host : "127.0.0.1",
                           (uint16_t)port_value, device_id, peer_id,
                           incoming, &simulator);

    falafel_app_config_t config = {
        .next_message_id = next_id,
        .current_time = current_time,
        .send_event = send_event,
        .user_data = &simulator,
        .device_id = device_id,
        .peer_id = peer_id,
    };
    if (falafel_app_init(&simulator.app, &config) != FALAFEL_APP_OK) {
        return EXIT_FAILURE;
    }
    load_result = desktop_state_load(simulator.state_path, &simulator.app,
                                     &simulator.reserved_sequence);
    if (load_result != DESKTOP_STATE_OK &&
        load_result != DESKTOP_STATE_NOT_FOUND) {
        fprintf(stderr, "Persistent state is corrupt or unsupported: %s\n",
                simulator.state_path);
        return EXIT_FAILURE;
    }
    simulator.next_sequence = simulator.reserved_sequence;

    lv_init();
    lv_display_t *display = lv_sdl_window_create(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    if (display == NULL) return EXIT_FAILURE;
    lv_sdl_window_set_title(display, device_text);
    lv_sdl_mouse_create();
    SDL_Window *window = lv_sdl_window_get_window(display);
    trackpad_navigation_init(window != NULL ? SDL_GetWindowID(window) : 0U,
                             false);
    const falafel_gui_callbacks_t callbacks = {
        .on_send_message = gui_send,
        .on_personality_changed = gui_personality,
        .on_messages_opened = gui_messages,
        .user_data = &simulator,
    };
    falafel_gui_set_callbacks(&callbacks);
    falafel_gui_create();
    falafel_gui_set_personality_remote(
        (falafel_personality_t)simulator.app.personality);
    falafel_gui_set_battery_percent(82U);
    for (size_t index = 0; index < simulator.app.received_count; ++index) {
        show_received_text(&simulator, &simulator.app.received[index]);
    }

    uint32_t last_flush = 0U;
    for (;;) {
        desktop_transport_poll(&simulator.transport);
        bool connected = desktop_transport_is_connected(&simulator.transport);
        falafel_gui_set_wifi_connected(connected);
        falafel_gui_set_peer_connected(false);
        uint32_t now = SDL_GetTicks();
        if (now - last_flush >= 1000U) {
            falafel_app_t before_flush = simulator.app;
            (void)falafel_app_flush(&simulator.app);
            if (memcmp(&before_flush.queue, &simulator.app.queue,
                       sizeof(simulator.app.queue)) != 0 &&
                !save_state(&simulator)) {
                simulator.app = before_flush;
                desktop_transport_force_reconnect(&simulator.transport);
            }
            last_flush = now;
        }
        uint32_t wait = lv_timer_handler();
        trackpad_navigation_process();
        SDL_Delay(wait < 1U ? 1U : wait > 16U ? 16U : wait);
    }
}
