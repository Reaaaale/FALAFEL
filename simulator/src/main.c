#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "SDL2/SDL.h"
#include "falafel_gui.h"
#include "lvgl.h"
#include "src/drivers/sdl/lv_sdl_mouse.h"
#include "src/drivers/sdl/lv_sdl_window.h"
#include "trackpad_navigation.h"

enum {
    DISPLAY_WIDTH = 320,
    DISPLAY_HEIGHT = 480,
};

static const char *personality_name(falafel_personality_t personality)
{
    static const char *names[] = {"TAXI", "CUOCO", "PROMEMORIA"};
    return personality < FALAFEL_PERSONALITY_COUNT ? names[personality] : "?";
}

static void on_send_message(const char *text, void *user_data)
{
    (void)user_data;
    printf("[GUI] Messaggio da inviare: %s\n", text);
    fflush(stdout);
}

static void on_personality_changed(falafel_personality_t personality, void *user_data)
{
    (void)user_data;
    printf("[GUI] Personalita selezionata: %s\n", personality_name(personality));
    fflush(stdout);
}

static void on_messages_opened(void *user_data)
{
    (void)user_data;
    puts("[GUI] Schermata messaggi aperta");
    fflush(stdout);
}

int main(void)
{
    lv_init();

    lv_display_t *display = lv_sdl_window_create(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    if (display == NULL) {
        fprintf(stderr, "Unable to create the LVGL SDL display.\n");
        return EXIT_FAILURE;
    }

    lv_sdl_window_set_title(display, "Falafel Device Simulator");
    lv_sdl_mouse_create();
    SDL_Window *window = lv_sdl_window_get_window(display);
    bool input_debug = getenv("FALAFEL_INPUT_DEBUG") != NULL;
    trackpad_navigation_init(window != NULL ? SDL_GetWindowID(window) : 0, input_debug);

    SDL_version linked_version;
    SDL_GetVersion(&linked_version);
    printf("[INPUT] SDL %u.%u.%u; horizontal trackpad navigation enabled%s\n",
           linked_version.major, linked_version.minor, linked_version.patch,
           input_debug ? " (debug logging on)" : "");
    const falafel_gui_callbacks_t callbacks = {
        .on_send_message = on_send_message,
        .on_personality_changed = on_personality_changed,
        .on_messages_opened = on_messages_opened,
        .user_data = NULL,
    };
    falafel_gui_set_callbacks(&callbacks);
    falafel_gui_create();
    falafel_gui_set_wifi_connected(true);
    falafel_gui_set_peer_connected(true);
    falafel_gui_set_battery_percent(82);
    falafel_gui_add_received_message("Arrivo tra dieci minuti.");
    falafel_gui_add_sent_message("Perfetto, ti aspetto qui!");
    falafel_gui_add_received_message("Porto anche qualcosa da mangiare?");
    falafel_gui_set_unread_count(2);
    falafel_gui_show_notification("Falafel collegato");

    for (;;) {
        uint32_t sleep_ms = lv_timer_handler();
        trackpad_navigation_process();
        if (sleep_ms < 1) {
            sleep_ms = 1;
        } else if (sleep_ms > 16) {
            sleep_ms = 16;
        }
        SDL_Delay(sleep_ms);
    }

    trackpad_navigation_deinit();
}
