#include "ui_smoke.h"
#include "falafel_gui_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned message_opens, personality_changes;
static void personality_changed(falafel_personality_t value, void *data)
{
    (void)value; (void)data; ++personality_changes;
}
static bool callback_state_error;
static void messages_opened(void *data)
{
    (void)data;
    if (!falafel_gui_is_messages_active() || falafel_gui.unread_count) callback_state_error = true;
    ++message_opens;
}

static lv_obj_t *button_named(lv_obj_t *root, const char *text)
{
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i) {
        lv_obj_t *child = lv_obj_get_child(root, (int32_t)i);
        if (lv_obj_check_type(child, &lv_label_class) &&
            strcmp(lv_label_get_text(child), text) == 0) return root;
        lv_obj_t *found = button_named(child, text);
        if (found) return found;
    }
    return NULL;
}

static void pointer(SDL_Window *window, Uint32 type, int32_t x, int32_t y)
{
    SDL_Event event = {0};
    event.type = type;
    if (type == SDL_MOUSEMOTION) {
        event.motion.windowID = SDL_GetWindowID(window);
        event.motion.x = x; event.motion.y = y;
        event.motion.state = SDL_BUTTON_LMASK;
    } else {
        event.button.windowID = SDL_GetWindowID(window);
        event.button.button = SDL_BUTTON_LEFT;
        event.button.state = type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
        event.button.x = x; event.button.y = y;
    }
    SDL_PushEvent(&event);
}

static bool click(SDL_Window *window, lv_obj_t *root, const char *text, bool down)
{
    lv_obj_update_layout(root);
    lv_obj_t *button = button_named(root, text);
    if (!button) return false;
    lv_area_t bounds;
    lv_obj_get_coords(button, &bounds);
    pointer(window, down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP,
            (bounds.x1 + bounds.x2) / 2, (bounds.y1 + bounds.y2) / 2);
    return true;
}

static void key(SDL_Window *window, Uint32 type, bool repeat, bool foreign)
{
    SDL_Event event = {0};
    event.type = type;
    event.key.windowID = SDL_GetWindowID(window) + (foreign ? 1 : 0);
    event.key.keysym.sym = SDLK_F2;
    event.key.repeat = repeat;
    event.key.state = type == SDL_KEYDOWN ? SDL_PRESSED : SDL_RELEASED;
    SDL_PushEvent(&event);
}

static void wheel(SDL_Window *window)
{
    SDL_Event event = {0};
    event.type = SDL_MOUSEWHEEL;
    event.wheel.windowID = SDL_GetWindowID(window);
    event.wheel.timestamp = SDL_GetTicks();
    event.wheel.preciseX = -2.0f;
    event.wheel.x = -2;
    SDL_PushEvent(&event);
}

/* Check every navigation hit target, not just its center, against the circle. */
static bool targets_inside_circle(lv_obj_t *root, int32_t size)
{
    if (lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) return true;
    if (lv_obj_check_type(root, &lv_button_class)) {
        lv_area_t a;
        lv_obj_get_coords(root, &a);
        for (int i = 0; i < 4; ++i) {
            int32_t dx = 2 * (i & 1 ? a.x1 : a.x2) - size;
            int32_t dy = 2 * (i & 2 ? a.y1 : a.y2) - size;
            if (dx * dx + dy * dy > size * size) return false;
        }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i)
        if (!targets_inside_circle(lv_obj_get_child(root, (int32_t)i), size)) return false;
    return true;
}

static bool snapshot(const char *name, int32_t size)
{
    const char *directory = getenv("FALAFEL_SCREENSHOTS");
    if (!directory) return true;
    lv_draw_buf_t *buffer = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_ARGB8888);
    if (!buffer) return false;
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(buffer->data,
        (int)buffer->header.w, (int)buffer->header.h, 32, (int)buffer->header.stride,
        SDL_PIXELFORMAT_ARGB8888);
    char path[1024];
    snprintf(path, sizeof(path), "%s/navigation_%s_%d.bmp", directory, name, (int)size);
    bool okay = surface && SDL_SaveBMP(surface, path) == 0;
    if (surface) SDL_FreeSurface(surface);
    lv_draw_buf_destroy(buffer);
    return okay;
}

int ui_navigation_step(lv_display_t *display, SDL_Window *window)
{
    static unsigned phase;
    static uint32_t last;
    static uint8_t pose;
    static int32_t menu_y;
    static uint32_t rotation_started;
    uint32_t now = SDL_GetTicks();
    if (now - last < 60) return 0; /* Distinct SDL/LVGL input polling cycles. */
    last = now;
    int32_t size = lv_display_get_horizontal_resolution(display);
    lv_obj_update_layout(falafel_gui.content);
    if (!targets_inside_circle(falafel_gui.content, size)) goto failure;
    lv_obj_t *root = NULL;
    const char *target = NULL;
    switch (phase) {
    case 0: {
        falafel_gui_callbacks_t callbacks = {.on_messages_opened = messages_opened,
            .on_personality_changed = personality_changed};
        falafel_gui_set_callbacks(&callbacks);
        falafel_gui_set_personality_remote(FALAFEL_PERSONALITY_COOK);
        falafel_gui_set_unread_count(5);
        pose = falafel_gui.rotation_frame;
        if (!snapshot("home", size)) goto failure;
        break;
    }
    case 1:
        if (button_named(falafel_gui.home, "Menu")) goto failure;
        key(window, SDL_KEYDOWN, false, false);
        break;
    case 2:
        key(window, SDL_KEYDOWN, true, false);
        key(window, SDL_KEYDOWN, false, false); /* Held duplicate. */
        break;
    case 3:
        if (falafel_gui.page != FALAFEL_PAGE_MENU || falafel_gui.unread_count != 5 || message_opens)
            goto failure;
        if (!snapshot("menu", size)) goto failure;
        wheel(window);
        break;
    case 4:
        key(window, SDL_KEYUP, false, false);
        if (falafel_gui_next_personality() || falafel_gui_previous_personality()) goto failure;
        break;
    case 5: case 6: root = falafel_gui.menu; target = "Messaggi"; break;
    case 7:
        if (!falafel_gui_is_messages_active() || message_opens != 1 || falafel_gui.unread_count)
            goto failure;
        if (!snapshot("messages", size)) goto failure;
        break;
    case 8: case 9: root = falafel_gui.messages; target = LV_SYMBOL_LEFT " Indietro"; break;
    case 10:
        if (falafel_gui.page != FALAFEL_PAGE_MENU) goto failure;
        break;
    case 11: case 12: root = falafel_gui.menu; target = "Giochi"; break;
    case 13:
        if (falafel_gui.page != FALAFEL_PAGE_GAMES || !snapshot("games", size)) goto failure;
        wheel(window);
        pointer(window, SDL_MOUSEBUTTONDOWN, size * 3 / 4, size / 2);
        break;
    case 14: pointer(window, SDL_MOUSEMOTION, size / 4, size / 2); break;
    case 15: pointer(window, SDL_MOUSEBUTTONUP, size / 4, size / 2); break;
    case 16:
        if (falafel_gui.page != FALAFEL_PAGE_GAMES) goto failure;
        break;
    case 17: case 18: root = falafel_gui.games; target = LV_SYMBOL_LEFT " Menu"; break;
    case 19:
        if (falafel_gui.page != FALAFEL_PAGE_MENU) goto failure;
        break;
    case 20: case 21: root = falafel_gui.menu; target = "Impostazioni"; break;
    case 22:
        if (falafel_gui.page != FALAFEL_PAGE_SETTINGS || !snapshot("settings", size)) goto failure;
        wheel(window);
        break;
    case 23: case 24: root = falafel_gui.settings; target = LV_SYMBOL_LEFT " Menu"; break;
    case 25:
        if (falafel_gui.page != FALAFEL_PAGE_MENU) goto failure;
        break;
    case 26: case 27: root = falafel_gui.menu; target = LV_SYMBOL_LEFT " Home"; break;
    case 28:
        if (!falafel_gui_is_home_active() || falafel_gui.personality != FALAFEL_PERSONALITY_COOK ||
            falafel_gui.personality_animating || falafel_gui.rotation_frame != pose) goto failure;
        break;
    case 29: case 30: root = falafel_gui.home; target = LV_SYMBOL_ENVELOPE; break;
    case 31:
        if (!falafel_gui_is_messages_active() || message_opens != 2) goto failure;
        break;
    case 32: case 33: root = falafel_gui.messages; target = LV_SYMBOL_LEFT " Indietro"; break;
    case 34:
        if (!falafel_gui_is_home_active()) goto failure;
        /* Repeated route visits keep exactly the same eagerly-created pages. */
        for (int i = 0; i < 100; ++i) {
            falafel_gui_open_menu();
            falafel_gui_navigate(FALAFEL_PAGE_GAMES); falafel_gui_go_back();
            falafel_gui_navigate(FALAFEL_PAGE_SETTINGS); falafel_gui_go_back();
            falafel_gui_go_back();
        }
        if (!falafel_gui_is_home_active() || message_opens != 2 || falafel_gui.personality_animating)
            goto failure;
        break;
    case 35: {
        /* The former touch Menu position is now just Home background. */
        menu_y = size * 93 / 100;
        pointer(window, SDL_MOUSEBUTTONDOWN, size / 2 + 28, menu_y);
        break;
    }
    case 36: pointer(window, SDL_MOUSEMOTION, size / 2 - 28, menu_y); break;
    case 37:
        pointer(window, SDL_MOUSEBUTTONUP, size / 2 - 28, menu_y);
        rotation_started = now;
        break;
    case 38:
        if (!falafel_gui_is_home_active()) goto failure; /* Home background swipe stays on Home. */
        if (falafel_gui.personality_animating) {
            if (now - rotation_started > 4000) goto failure;
            return 0;
        }
        if (callback_state_error || personality_changes != 1 || falafel_gui.personality != FALAFEL_PERSONALITY_REMINDER ||
            falafel_gui.rotation_frame != pose || message_opens != 2) goto failure;
        break;
    case 39:
        falafel_gui_navigate(FALAFEL_PAGE_SETTINGS);
        key(window, SDL_KEYDOWN, false, true); /* Other window ignored. */
        break;
    case 40:
        if (falafel_gui.page != FALAFEL_PAGE_SETTINGS) goto failure;
        key(window, SDL_KEYDOWN, true, false); /* Repeat without press ignored. */
        break;
    case 41:
        if (falafel_gui.page != FALAFEL_PAGE_SETTINGS) goto failure;
        key(window, SDL_KEYDOWN, false, false);
        break;
    case 42:
        if (falafel_gui.page != FALAFEL_PAGE_MENU) goto failure;
        falafel_gui_go_back();
        key(window, SDL_KEYDOWN, false, false); /* Still held: no second press. */
        break;
    case 43: {
        if (!falafel_gui_is_home_active()) goto failure;
        SDL_Event event = {0}; event.type = SDL_WINDOWEVENT;
        event.window.windowID = SDL_GetWindowID(window);
        event.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
        SDL_PushEvent(&event); /* Release lost when window loses focus. */
        key(window, SDL_KEYDOWN, true, false);
        break;
    }
    case 44:
        if (!falafel_gui_is_home_active()) goto failure;
        if (!falafel_gui_previous_personality()) goto failure;
        key(window, SDL_KEYDOWN, false, false); /* Works during rotation. */
        break;
    case 45:
        if (falafel_gui.page != FALAFEL_PAGE_MENU) goto failure;
        key(window, SDL_KEYUP, false, false);
        rotation_started = now;
        break;
    case 46:
        if (falafel_gui.personality_animating) {
            if (now - rotation_started > 4000) goto failure;
            return 0;
        }
        if (falafel_gui.page != FALAFEL_PAGE_MENU || message_opens != 2 ||
            personality_changes != 2 || falafel_gui.rotation_frame != pose ||
            falafel_gui.personality != FALAFEL_PERSONALITY_COOK) goto failure;
        printf("[NAV] PASS %dpx: SDL F2 physical-button simulation, held/repeat/window/focus guards, "
               "menu during rotation, three entries/back paths, hidden-Home gestures, "
               "unread/callback semantics, pose continuity, 100 navigation cycles\n", (int)size);
        return 1;
    }
    if (root && !click(window, root, target, phase == 1 || phase == 5 || phase == 8 ||
        phase == 11 || phase == 17 || phase == 20 || phase == 23 || phase == 26 ||
        phase == 29 || phase == 32)) goto failure;
    ++phase;
    return 0;
failure:
    fprintf(stderr, "[NAV] FAIL phase %u, page %d, size %d\n", phase, falafel_gui.page, (int)size);
    return -1;
}
