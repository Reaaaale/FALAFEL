#include "falafel_gui_internal.h"

static const char *names[FALAFEL_PERSONALITY_COUNT] = {"Taxi", "Cuoco", "Promemoria"};
static lv_obj_t *title_label, *art_stage, *character_image, *message_button;
static lv_timer_t *rotation_timer;
static falafel_personality_t pending_personality;
static lv_obj_t *markers[FALAFEL_PERSONALITY_COUNT];

enum {
    HOME_SWIPE_MIN_DISTANCE = 50,
    HOME_SWIPE_AXIS_BIAS_PERCENT = 125,
    HOME_FRAME_MS = 30,
};

typedef struct {
    lv_point_t start;
    bool pressed;
    bool consumed;
    bool blocked_at_press;
} home_pointer_gesture_t;

static home_pointer_gesture_t pointer_gesture;

static void messages_clicked(lv_event_t *event)
{
    (void)event;
    if (pointer_gesture.consumed) return;
    falafel_gui_open_messages();
}

/* The timer only switches compressed descriptors. LVGL draws/decodes one PNG
 * at a time with its image cache disabled; there is no playback loop or sleep. */
static void stop_rotation(void)
{
    lv_timer_pause(rotation_timer);
    falafel_gui.personality_animating = false;
    lv_image_set_src(character_image, &falafel_asset_character);
}

static void rotation_step(lv_timer_t *timer)
{
    (void)timer;
    if (++falafel_gui.rotation_frame < FALAFEL_ROTATION_FRAME_COUNT) {
        lv_image_set_src(character_image,
                         &falafel_asset_rotation[falafel_gui.rotation_frame]);
        return;
    }
    stop_rotation();
    falafel_gui.personality = pending_personality;
    falafel_home_refresh();
    if (falafel_gui.callbacks.on_personality_changed != NULL)
        falafel_gui.callbacks.on_personality_changed(pending_personality, falafel_gui.callbacks.user_data);
}

static bool select_personality(falafel_personality_t selected)
{
    if (falafel_gui.personality_animating || selected >= FALAFEL_PERSONALITY_COUNT ||
        selected == falafel_gui.personality) return false;
    pending_personality = selected;
    falafel_gui.rotation_frame = 0;
    falafel_gui.personality_animating = true;
    lv_image_set_src(character_image, &falafel_asset_rotation[0]);
    lv_timer_reset(rotation_timer);
    lv_timer_resume(rotation_timer);
    return true;
}

bool falafel_gui_set_personality_remote(falafel_personality_t selected)
{
    if (selected >= FALAFEL_PERSONALITY_COUNT) return false;
    /* Synchronized state supersedes a pending local choice, even if it equals
     * the current personality. Never emit a stale local callback afterwards. */
    if (falafel_gui.personality_animating) stop_rotation();
    if (selected == falafel_gui.personality) return false;
    falafel_gui.personality = selected;
    falafel_home_refresh();
    return true;
}

static void home_deleted(lv_event_t *event)
{
    (void)event;
    lv_timer_delete(rotation_timer);
    rotation_timer = NULL;
    falafel_gui.personality_animating = false;
}

bool falafel_gui_next_personality(void)
{
    if (falafel_gui.personality >= FALAFEL_PERSONALITY_REMINDER) return false;
    return select_personality((falafel_personality_t)(falafel_gui.personality + 1));
}

bool falafel_gui_previous_personality(void)
{
    if (falafel_gui.personality <= FALAFEL_PERSONALITY_TAXI) return false;
    return select_personality((falafel_personality_t)(falafel_gui.personality - 1));
}

static void home_pointer_event(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_indev_t *indev = lv_event_get_indev(event);
    if (indev == NULL || lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER) return;

    if (code == LV_EVENT_PRESSED) {
        lv_indev_get_point(indev, &pointer_gesture.start);
        pointer_gesture.pressed = true;
        pointer_gesture.consumed = false;
        pointer_gesture.blocked_at_press = falafel_gui.personality_animating;
        return;
    }
    if (code == LV_EVENT_PRESS_LOST) {
        pointer_gesture.pressed = false;
        return;
    }
    if (code != LV_EVENT_RELEASED || !pointer_gesture.pressed) return;

    lv_point_t end;
    lv_indev_get_point(indev, &end);
    pointer_gesture.pressed = false;
    int32_t dx = end.x - pointer_gesture.start.x;
    int32_t dy = end.y - pointer_gesture.start.y;
    int32_t abs_dx = dx < 0 ? -dx : dx;
    int32_t abs_dy = dy < 0 ? -dy : dy;

    if (abs_dx < HOME_SWIPE_MIN_DISTANCE ||
        abs_dx * 100 < abs_dy * HOME_SWIPE_AXIS_BIAS_PERCENT) return;

    pointer_gesture.consumed = true;
    if (pointer_gesture.blocked_at_press) return;
    if (dx < 0) falafel_gui_next_personality();
    else falafel_gui_previous_personality();
}

static void add_pointer_handler_to_tree(lv_obj_t *object)
{
    lv_obj_add_event_cb(object, home_pointer_event, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(object, home_pointer_event, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(object, home_pointer_event, LV_EVENT_PRESS_LOST, NULL);
    uint32_t child_count = lv_obj_get_child_count(object);
    for (uint32_t i = 0; i < child_count; ++i)
        add_pointer_handler_to_tree(lv_obj_get_child(object, (int32_t)i));
}

lv_obj_t *falafel_home_create(lv_obj_t *parent)
{
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, 466, 466);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    art_stage = lv_obj_create(root);
    lv_obj_remove_style_all(art_stage);
    lv_obj_set_size(art_stage, 320, 320);
    lv_obj_align(art_stage, LV_ALIGN_CENTER, 0, 4);
    lv_obj_clear_flag(art_stage, LV_OBJ_FLAG_SCROLLABLE);
    character_image = lv_image_create(art_stage);
    lv_obj_t *image = character_image;
    lv_image_set_src(image, &falafel_asset_character);
    /* LVGL scale unit is 256. Uniform fit in a 320px square, no cropping. */
    uint32_t edge = falafel_asset_character.header.w > falafel_asset_character.header.h
        ? falafel_asset_character.header.w : falafel_asset_character.header.h;
    lv_image_set_scale(image, (uint32_t)(320u * 256u / edge));
    lv_obj_center(image);

    lv_obj_t *notification = lv_button_create(root);
    message_button = notification;
    lv_obj_set_size(notification, 44, 40);
    lv_obj_align(notification, LV_ALIGN_TOP_MID, 0, 94);
    falafel_theme_style_button(notification, FALAFEL_COLOR_ACCENT);
    lv_obj_set_style_radius(notification, 14, 0);
    lv_obj_set_style_border_width(notification, 0, 0);
    lv_obj_t *icon = falafel_theme_create_label(notification, LV_SYMBOL_ENVELOPE,
                                               &lv_font_montserrat_18, FALAFEL_COLOR_CREAM);
    lv_obj_center(icon);
    lv_obj_add_event_cb(notification, messages_clicked, LV_EVENT_CLICKED, NULL);

    title_label = falafel_theme_create_label(root, "Taxi", &lv_font_montserrat_16,
                                              FALAFEL_COLOR_WHITE);
    lv_obj_align(title_label, LV_ALIGN_BOTTOM_MID, 0, -76);
    for (int i = 0; i < FALAFEL_PERSONALITY_COUNT; ++i) {
        markers[i] = lv_obj_create(root);
        lv_obj_remove_style_all(markers[i]);
        lv_obj_set_size(markers[i], 5, 5);
        lv_obj_align(markers[i], LV_ALIGN_BOTTOM_MID, (i - 1) * 14, -57);
        lv_obj_set_style_bg_opa(markers[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(markers[i], LV_RADIUS_CIRCLE, 0);
    }
    rotation_timer = lv_timer_create(rotation_step, HOME_FRAME_MS, NULL);
    lv_timer_pause(rotation_timer);
    lv_obj_add_event_cb(root, home_deleted, LV_EVENT_DELETE, NULL);
    add_pointer_handler_to_tree(root);
    falafel_home_refresh();
    return root;
}

void falafel_home_refresh(void)
{
    lv_obj_set_style_bg_color(message_button, lv_color_hex(falafel_gui.unread_count
        ? FALAFEL_COLOR_ACCENT : FALAFEL_COLOR_SURFACE), 0);
    lv_label_set_text(title_label, names[falafel_gui.personality]);
    for (int i = 0; i < FALAFEL_PERSONALITY_COUNT; ++i)
        lv_obj_set_style_bg_color(markers[i], lv_color_hex(
            i == (int)falafel_gui.personality ? FALAFEL_COLOR_ACCENT : FALAFEL_COLOR_MUTED), 0);
}
