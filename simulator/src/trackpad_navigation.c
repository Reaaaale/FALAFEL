#include "trackpad_navigation.h"

#include <math.h>
#include <stdio.h>

#include "SDL2/SDL.h"
#include "falafel_gui.h"

enum {
    TRACKPAD_QUIET_MS = 220,
};

static const float TRACKPAD_SWIPE_THRESHOLD = 1.5f;
static const float TRACKPAD_AXIS_BIAS = 1.25f;

typedef struct {
    uint32_t window_id;
    uint32_t last_event_ms;
    float horizontal;
    float vertical_abs;
    bool latched;
    bool debug_logging;
    bool installed;
} trackpad_state_t;

static trackpad_state_t state;

static int SDLCALL watch_sdl_event(void *userdata, SDL_Event *event)
{
    trackpad_state_t *trackpad = userdata;
    if (event->type != SDL_MOUSEWHEEL ||
        (trackpad->window_id != 0 && event->wheel.windowID != trackpad->window_id)) return 1;

    float x = event->wheel.preciseX;
    float y = event->wheel.preciseY;

    /* SDL marks macOS natural scrolling as FLIPPED. Undo that inversion so
       left/right describe the physical two-finger gesture consistently. */
    if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
        x = -x;
        y = -y;
    }

    trackpad->last_event_ms = event->wheel.timestamp;
    trackpad->horizontal += x;
    trackpad->vertical_abs += fabsf(y);

    if (trackpad->debug_logging && x != 0.0f) {
        fprintf(stderr,
                "[INPUT] SDL_MOUSEWHEEL x=%+.3f y=%+.3f direction=%s accumulated_x=%+.3f\n",
                x, y,
                event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? "flipped" : "normal",
                trackpad->horizontal);
    }
    return 1; /* Observe the event; LVGL's single SDL event loop still consumes it. */
}

void trackpad_navigation_init(uint32_t window_id, bool debug_logging)
{
    state.window_id = window_id;
    state.debug_logging = debug_logging;
    SDL_AddEventWatch(watch_sdl_event, &state);
    state.installed = true;
}

void trackpad_navigation_process(void)
{
    uint32_t now = SDL_GetTicks();
    uint32_t quiet_for = now - state.last_event_ms;

    if (!falafel_gui_is_home_active()) {
        state.horizontal = 0.0f;
        state.vertical_abs = 0.0f;
        state.latched = false;
        return;
    }

    if (state.latched) {
        if (quiet_for >= TRACKPAD_QUIET_MS) {
            state.horizontal = 0.0f;
            state.vertical_abs = 0.0f;
            state.latched = false;
        }
        return;
    }

    float horizontal_abs = fabsf(state.horizontal);
    if (horizontal_abs < TRACKPAD_SWIPE_THRESHOLD ||
        horizontal_abs < state.vertical_abs * TRACKPAD_AXIS_BIAS) {
        if (state.last_event_ms != 0 && quiet_for >= TRACKPAD_QUIET_MS) {
            state.horizontal = 0.0f;
            state.vertical_abs = 0.0f;
        }
        return;
    }

    bool changed = state.horizontal < 0.0f
        ? falafel_gui_next_personality()
        : falafel_gui_previous_personality();
    state.latched = true;
    fprintf(stderr, "[INPUT] Trackpad swipe %s%s\n",
            state.horizontal < 0.0f ? "left" : "right",
            changed ? " recognized" : " (personality limit)");
}

void trackpad_navigation_deinit(void)
{
    if (state.installed) SDL_DelEventWatch(watch_sdl_event, &state);
    state.installed = false;
}
