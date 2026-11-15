#include "trackpad_navigation.h"

#include <math.h>
#include <stdatomic.h>
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
    bool gesture_active;
    bool blocked_at_start;
    bool debug_logging;
    bool installed;
} trackpad_state_t;

static trackpad_state_t state;
/* SDL watches may run on an input thread: never inspect LVGL objects there. */
static atomic_bool input_blocked;

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

    if (!trackpad->gesture_active ||
        event->wheel.timestamp - trackpad->last_event_ms >= TRACKPAD_QUIET_MS) {
        trackpad->horizontal = 0.0f;
        trackpad->vertical_abs = 0.0f;
        trackpad->latched = false;
        trackpad->gesture_active = true;
        trackpad->blocked_at_start = atomic_load_explicit(&input_blocked, memory_order_acquire);
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
    state = (trackpad_state_t){.window_id = window_id, .debug_logging = debug_logging};
    atomic_init(&input_blocked, true);
    SDL_AddEventWatch(watch_sdl_event, &state);
    state.installed = true;
}

void trackpad_navigation_process(void)
{
    bool blocked = !falafel_gui_is_home_active() || falafel_gui_is_personality_animating();
    atomic_store_explicit(&input_blocked, blocked, memory_order_release);
    uint32_t now = SDL_GetTicks();
    uint32_t quiet_for = now - state.last_event_ms;

    if (!state.gesture_active) return;
    if (quiet_for >= TRACKPAD_QUIET_MS) {
        state.horizontal = 0.0f;
        state.vertical_abs = 0.0f;
        state.latched = false;
        state.gesture_active = false;
        return;
    }
    if (blocked || state.blocked_at_start) {
        /* Do not queue a partial/busy gesture for when playback completes.
         * Keep its latch through the inertial tail until a quiet interval. */
        state.horizontal = 0.0f;
        state.vertical_abs = 0.0f;
        state.latched = true;
        return;
    }
    if (state.latched) return;

    float horizontal_abs = fabsf(state.horizontal);
    if (horizontal_abs < TRACKPAD_SWIPE_THRESHOLD ||
        horizontal_abs < state.vertical_abs * TRACKPAD_AXIS_BIAS) {
        return;
    }

    bool changed = state.horizontal < 0.0f
        ? falafel_gui_next_personality()
        : falafel_gui_previous_personality();
    state.latched = true;
    if (changed) atomic_store_explicit(&input_blocked, true, memory_order_release);
    fprintf(stderr, "[INPUT] Trackpad swipe %s%s\n",
            state.horizontal < 0.0f ? "left" : "right",
            changed ? " recognized" : " (personality limit)");
}

void trackpad_navigation_deinit(void)
{
    if (state.installed) SDL_DelEventWatch(watch_sdl_event, &state);
    state.installed = false;
}
