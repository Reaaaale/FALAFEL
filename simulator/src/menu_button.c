#include "menu_button.h"
#include <stdatomic.h>
#include "SDL2/SDL.h"
#include "falafel_gui.h"

static uint32_t target_window;
static atomic_bool held, pending;

/* SDL watches can run outside the GUI thread. Only latch input here. */
static int SDLCALL watch_button(void *user, SDL_Event *event)
{
    (void)user;
    if (event->type == SDL_WINDOWEVENT && event->window.windowID == target_window &&
        event->window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
        atomic_store(&held, false);
        atomic_store(&pending, false);
    }
    if ((event->type == SDL_KEYDOWN || event->type == SDL_KEYUP) &&
        event->key.windowID == target_window && event->key.keysym.sym == SDLK_F2) {
        if (event->type == SDL_KEYUP) atomic_store(&held, false);
        else if (!event->key.repeat && !atomic_exchange(&held, true))
            atomic_store(&pending, true);
    }
    return 1; /* LVGL retains ownership of the SDL event queue. */
}

void menu_button_init(uint32_t window_id)
{
    target_window = window_id;
    atomic_init(&held, false);
    atomic_init(&pending, false);
    SDL_AddEventWatch(watch_button, NULL);
}

void menu_button_process(void)
{
    if (atomic_exchange(&pending, false)) falafel_gui_menu_button_pressed();
}

void menu_button_deinit(void)
{
    SDL_DelEventWatch(watch_button, NULL);
}
