#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "SDL2/SDL.h"
#include "messaging_controller.h"
#include "falafel_gui.h"
#include "lvgl.h"
#include "src/drivers/sdl/lv_sdl_mouse.h"
#include "src/drivers/sdl/lv_sdl_window.h"
#include "trackpad_navigation.h"
#include "ui_smoke.h"

static volatile sig_atomic_t interrupted;
static atomic_bool close_requested;
static void signal_stop(int signum) { (void)signum; interrupted = 1; }
static int exit_filter(void *user, SDL_Event *event)
{
    uint32_t window = *(uint32_t *)user;
    if (event->type == SDL_QUIT || (event->type == SDL_WINDOWEVENT &&
        event->window.windowID == window && event->window.event == SDL_WINDOWEVENT_CLOSE)) {
        atomic_store(&close_requested, true);
        return 0; /* Keep LVGL alive while the worker closes storage/network. */
    }
    return 1;
}
typedef struct {
    messaging_controller *controller;
    uint16_t unread;
    unsigned received, stored, delivered;
    bool ready, mark_read_pending;
} simulator_t;

static bool gui_send(const char *text, void *user)
{
    simulator_t *s = user; uint64_t request;
    if (!messaging_try_send(s->controller, text, strlen(text), &request)) return false;
    printf("[E2EE] REQUEST %llu\n", (unsigned long long)request);
    falafel_gui_show_notification("Richiesta inviata al worker");
    return true;
}
static void gui_messages(void *user) { ((simulator_t *)user)->unread = 0; ((simulator_t *)user)->mark_read_pending=true; }
static void gui_personality(falafel_personality_t value, void *user)
{ (void)user; printf("[UI] PERSONALITY local=%u\n", (unsigned)value); }
static void log_event(const char *name, const messaging_event *event)
{
    printf("[E2EE] %s ", name);
    for (unsigned i = 0; i < 16; ++i) printf("%02x", event->id[i]);
    printf(" restored=%u sent=%u delivery=%u read=%u\n", event->restored,event->sent,event->delivery,event->read);
}
static bool dispatch(simulator_t *s)
{
    messaging_event event; bool fatal = false;
    /* Fixed work budget, no disk/network/crypto or waiting on the LVGL thread. */
    for (unsigned i = 0; i < 4 && messaging_try_event(s->controller, &event); ++i) {
        switch (event.kind) {
        case MESSAGING_READY: s->ready = true; puts("[E2EE] READY"); break;
        case MESSAGING_CONNECTION:
            falafel_gui_set_wifi_connected(event.connected);
            printf("[E2EE] CONNECTION %u\n", event.connected);
            break;
        case MESSAGING_STORED:
            ++s->stored; log_event("STORED", &event);
            falafel_gui_message_update(event.id,event.sent,event.text,event.delivery,event.read);
            falafel_gui_show_notification("Salvato per l'invio"); break;
        case MESSAGING_DELIVERED:
            ++s->delivered; log_event("DELIVERED", &event);
            falafel_gui_message_update(event.id,event.sent,event.text,event.delivery,event.read);
            falafel_gui_show_notification("Accettato dal destinatario"); break;
        case MESSAGING_RECEIVED:
            ++s->received; log_event("RECEIVED", &event);
            falafel_gui_message_update(event.id,event.sent,event.text,event.delivery,event.read);
            if (!event.restored) falafel_gui_show_notification("Nuovo messaggio");
            break;
        case MESSAGING_HISTORY:
            log_event("HISTORY", &event);
            falafel_gui_message_update(event.id,event.sent,event.text,event.delivery,event.read);
            break;
        case MESSAGING_ERROR:
            fprintf(stderr, "[E2EE] ERROR code=%u fatal=%u request=%llu\n",
                event.error, event.fatal, (unsigned long long)event.request);
            falafel_gui_show_notification(event.error == MESSAGING_FULL ? "Archivio o mailbox pieni" :
                event.error == MESSAGING_IDENTITY_ERROR ? "Identita non valida" :
                event.error == MESSAGING_STORAGE_ERROR ? "Errore archivio messaggi" : "Errore messaggistica");
            fatal = fatal || event.fatal; break;
        }
        if(event.kind!=MESSAGING_ERROR && event.kind!=MESSAGING_READY && event.kind!=MESSAGING_CONNECTION) {
            if(falafel_gui_is_messages_active()) { s->mark_read_pending=true; s->unread=0; }
            else s->unread=(uint16_t)falafel_gui_message_unread_count();
            falafel_gui_set_unread_count(s->unread);
        }
        memset(&event, 0, sizeof(event));
    }
    return fatal;
}
static const char *argument(int argc, char **argv, const char *name, const char *fallback)
{
    for (int i = 1; i + 1 < argc; ++i) if (!strcmp(argv[i], name)) return argv[i + 1];
    return fallback;
}
static bool flag(int argc, char **argv, const char *name)
{
    for (int i = 1; i < argc; ++i) if (!strcmp(argv[i], name)) return true;
    return false;
}
static bool parse_u32(const char *text, uint32_t minimum, uint32_t maximum, uint32_t *value)
{
    char *end; unsigned long parsed;
    if (!text || !*text || *text == '-') return false;
    errno = 0; parsed = strtoul(text, &end, 10);
    if (errno || *end || parsed < minimum || parsed > maximum) return false;
    *value = (uint32_t)parsed; return true;
}
int main(int argc, char **argv)
{
    bool smoke = flag(argc, argv, "--ui-smoke-test");
    bool integration = flag(argc, argv, "--test-send-preset");
    const char *identity = argument(argc, argv, "--identity", NULL);
    const char *mailbox = argument(argc, argv, "--mailbox", NULL);
    uint32_t own, peer, port = 7777, duration = 0, test_count=1;
    if (!parse_u32(argument(argc, argv, "--device", getenv("FALAFEL_DEVICE_ID")), 1, UINT32_MAX - 1, &own) ||
        !parse_u32(argument(argc, argv, "--peer", getenv("FALAFEL_PEER_ID")), 1, UINT32_MAX - 1, &peer) ||
        own == peer || !parse_u32(argument(argc, argv, "--port", "7777"), 1, 65535, &port) ||
        (argument(argc, argv, "--test-duration-ms", NULL) &&
         !parse_u32(argument(argc, argv, "--test-duration-ms", NULL), 1, 60000, &duration)) ||
        !parse_u32(argument(argc,argv,"--test-send-count","1"),1,1000,&test_count) ||
        (!smoke && (!identity || !mailbox)) || (smoke && integration) ||
        flag(argc, argv, "--data")) {
        fprintf(stderr, "Usage: %s --device ID --peer ID --identity FILE --mailbox FILE "
            "[--host HOST] [--port PORT] [--history DIR] [--init-history]\n--data legacy state is no longer supported.\n", argv[0]);
        return EXIT_FAILURE;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGINT, signal_stop); signal(SIGTERM, signal_stop);
    atomic_init(&close_requested, false);
    simulator_t s = {0};
    if (!smoke) {
        messaging_config config = {.own = own, .peer = peer, .port = (uint16_t)port,
            .host = argument(argc, argv, "--host", "127.0.0.1"),
            .identity_path = identity, .mailbox_path = mailbox,
            .history_path=argument(argc,argv,"--history",NULL),.create_history=flag(argc,argv,"--init-history")};
        s.controller = messaging_start(&config);
        if (!s.controller) return EXIT_FAILURE;
    }
    lv_init();
    lv_display_t *display = lv_sdl_window_create(466, 466);
    if (!display) {
        messaging_request_stop(s.controller);
        /* No rendering loop exists on initialization failure. */
        while (!messaging_finished(s.controller)) SDL_Delay(1);
        messaging_destroy(s.controller); return EXIT_FAILURE;
    }
    char title[80]; snprintf(title, sizeof(title), "Falafel E2EE %u -> %u", own, peer);
    lv_sdl_window_set_title(display, title); lv_sdl_window_set_resizeable(display, false);
    lv_sdl_mouse_create(); SDL_Window *window = lv_sdl_window_get_window(display);
    uint32_t window_id = SDL_GetWindowID(window);
    SDL_SetEventFilter(exit_filter, &window_id);
    trackpad_navigation_init(window_id, false);
    falafel_gui_callbacks_t callbacks = {.on_send_message = gui_send, .on_personality_changed = gui_personality,
        .on_messages_opened = gui_messages, .user_data = &s};
    /* Personality changes stay local; no legacy event is transmitted. */
    falafel_gui_set_callbacks(&callbacks); falafel_gui_create();
    uint32_t started = SDL_GetTicks(), previous = started, max_gap = 0, loops = 0, rotation_started = 0;
    bool stopping = false; unsigned presets_sent=0; int result = EXIT_SUCCESS;
    for (;;) {
        uint32_t now = SDL_GetTicks();
        if (now - previous > max_gap) max_gap = now - previous;
        previous = now; ++loops;
        if(s.mark_read_pending && messaging_try_mark_read(s.controller)) s.mark_read_pending=false;
        if (dispatch(&s)) { result = EXIT_FAILURE; stopping = true; }
        if (duration && now - started >= duration) {
            SDL_Event close = {0}; close.type = SDL_WINDOWEVENT;
            close.window.windowID = window_id; close.window.event = SDL_WINDOWEVENT_CLOSE;
            SDL_PushEvent(&close); duration = 0;
        }
        if (interrupted || atomic_load(&close_requested)) stopping = true;
        if (!stopping && integration && s.ready) {
            if (!rotation_started) { falafel_gui_next_personality(); rotation_started = now; }
            else if (presets_sent<test_count && now - rotation_started >= 150) {
                /* Same action as the visible preset button, during animation. */
                if(!falafel_gui_is_messages_active()) falafel_gui_open_messages();
                if(falafel_gui_send_preset()) ++presets_sent;
            }
        }
        uint32_t wait = lv_timer_handler(); trackpad_navigation_process();
        if (smoke && !stopping) {
            int check = ui_smoke_step(display, window);
            if (check) { result = check > 0 ? EXIT_SUCCESS : EXIT_FAILURE; stopping = true; }
        }
        if (stopping) {
            messaging_request_stop(s.controller);
            if (messaging_finished(s.controller)) break;
        }
        SDL_Delay(wait < 1 ? 1 : wait > 8 ? 8 : wait);
    }
    if (messaging_failed(s.controller)) result = EXIT_FAILURE;
    lv_mem_monitor_t memory; lv_mem_monitor(&memory);
    printf("[UI] HEALTH loops=%u max_gap_ms=%u received=%u stored=%u delivered=%u rows=%u unread=%u worker_bytes=%zu view_bytes=%u lvgl_peak=%zu\n",
        loops, max_gap, s.received, s.stored, s.delivered,falafel_gui_message_count(),falafel_gui_message_unread_count(),
        messaging_static_memory(),falafel_gui_message_cache_bytes(),memory.max_used);
    /* Worker already finished; no network/storage join blocks rendering. */
    messaging_destroy(s.controller);
    trackpad_navigation_deinit(); SDL_SetEventFilter(NULL, NULL);
    lv_deinit();
    return result;
}
