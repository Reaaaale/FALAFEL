#include "falafel_gui_internal.h"

typedef struct {
    const char *name;
    const char *line;
    const char *code;
    const lv_image_dsc_t *image;
} personality_view_t;

static const personality_view_t personalities[FALAFEL_PERSONALITY_COUNT] = {
    {"TAXI", "Dove ti porto?", "NYC CAB / 01", &falafel_asset_taxi},
    {"CUOCO", "Cosa bolle in pentola?", "KITCHEN UNIT / 02", &falafel_asset_cook},
    {"PROMEMORIA", "Te lo segno. Stavolta.", "DON'T FORGET / 03", &falafel_asset_reminder},
};

static lv_obj_t *title_label, *line_label, *code_label;
static lv_obj_t *art_stage, *art_image;
static lv_obj_t *markers[FALAFEL_PERSONALITY_COUNT], *messages_label;

enum {
    HOME_SWIPE_MIN_DISTANCE = 50,
    HOME_SWIPE_AXIS_BIAS_PERCENT = 125,
    HOME_TRANSITION_MS = 160,
};

typedef struct {
    lv_point_t start;
    bool pressed;
    bool consumed;
} home_pointer_gesture_t;

static home_pointer_gesture_t pointer_gesture;

static void messages_clicked(lv_event_t *event)
{
    (void)event;
    if (pointer_gesture.consumed) return;
    falafel_gui_show_messages();
    if (falafel_gui.callbacks.on_messages_opened != NULL)
        falafel_gui.callbacks.on_messages_opened(falafel_gui.callbacks.user_data);
}

static void composer_clicked(lv_event_t *event)
{
    (void)event;
    if (!pointer_gesture.consumed) falafel_gui_show_composer();
}

static void animation_set_translate_x(void *object, int32_t value)
{
    lv_obj_set_style_translate_x((lv_obj_t *)object, value, 0);
}

static void animate_personality(int32_t from_x)
{
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, art_stage);
    lv_anim_set_exec_cb(&animation, animation_set_translate_x);
    lv_anim_set_values(&animation, from_x, 0);
    lv_anim_set_duration(&animation, HOME_TRANSITION_MS);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);
}

static bool select_personality(falafel_personality_t selected)
{
    if (selected >= FALAFEL_PERSONALITY_COUNT || selected == falafel_gui.personality) return false;
    int32_t direction = selected > falafel_gui.personality ? 1 : -1;
    falafel_gui.personality = selected;
    falafel_home_refresh();
    animate_personality(direction * 34);
    if (falafel_gui.callbacks.on_personality_changed != NULL)
        falafel_gui.callbacks.on_personality_changed(selected, falafel_gui.callbacks.user_data);
    return true;
}

bool falafel_gui_set_personality_remote(falafel_personality_t selected)
{
    if (selected >= FALAFEL_PERSONALITY_COUNT || selected == falafel_gui.personality) return false;
    falafel_gui.personality = selected;
    falafel_home_refresh();
    animate_personality(0);
    return true;
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

static lv_obj_t *create_action_button(lv_obj_t *parent, const char *text, uint32_t color)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, 136, 52);
    falafel_theme_style_button(button, color);
    lv_obj_t *label = falafel_theme_create_label(button, text, &lv_font_montserrat_14,
                                                  color == FALAFEL_COLOR_NIGHT
                                                      ? FALAFEL_COLOR_CREAM
                                                      : FALAFEL_COLOR_NIGHT);
    lv_obj_center(label);
    return button;
}

lv_obj_t *falafel_home_create(lv_obj_t *parent)
{
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_size(root, 320, 432);
    lv_obj_align(root, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    code_label = falafel_theme_create_label(root, "NYC CAB / 01", &lv_font_montserrat_12,
                                             FALAFEL_COLOR_HORN_BLUE);
    lv_obj_align(code_label, LV_ALIGN_TOP_LEFT, 18, 15);
    title_label = falafel_theme_create_label(root, "TAXI", &lv_font_montserrat_28, FALAFEL_COLOR_NIGHT);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 18, 36);
    line_label = falafel_theme_create_label(root, "Dove ti porto?", &lv_font_montserrat_16,
                                            FALAFEL_COLOR_NIGHT);
    lv_obj_align(line_label, LV_ALIGN_TOP_LEFT, 19, 72);

    art_stage = lv_obj_create(root);
    lv_obj_set_size(art_stage, 284, 178);
    lv_obj_align(art_stage, LV_ALIGN_TOP_MID, 0, 102);
    lv_obj_set_style_bg_color(art_stage, lv_color_hex(FALAFEL_COLOR_HORN_BLUE), 0);
    lv_obj_set_style_border_color(art_stage, lv_color_hex(FALAFEL_COLOR_NIGHT), 0);
    lv_obj_set_style_border_width(art_stage, 3, 0);
    lv_obj_set_style_radius(art_stage, 10, 0);
    lv_obj_set_style_pad_all(art_stage, 0, 0);
    lv_obj_clear_flag(art_stage, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *corner = lv_obj_create(art_stage);
    lv_obj_set_size(corner, 22, 7);
    lv_obj_align(corner, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_set_style_bg_color(corner, lv_color_hex(FALAFEL_COLOR_YELLOW), 0);
    lv_obj_set_style_border_width(corner, 0, 0);
    lv_obj_set_style_radius(corner, 0, 0);
    lv_obj_clear_flag(corner, LV_OBJ_FLAG_SCROLLABLE);

    art_image = lv_image_create(art_stage);
    lv_image_set_src(art_image, &falafel_asset_taxi);
    lv_obj_center(art_image);

    for (int i = 0; i < FALAFEL_PERSONALITY_COUNT; ++i) {
        markers[i] = lv_obj_create(root);
        lv_obj_set_size(markers[i], 78, 6);
        lv_obj_align(markers[i], LV_ALIGN_TOP_LEFT, 18 + i * 94, 298);
        lv_obj_set_style_border_width(markers[i], 0, 0);
        lv_obj_set_style_radius(markers[i], 0, 0);
        lv_obj_set_style_pad_all(markers[i], 0, 0);
        lv_obj_clear_flag(markers[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    lv_obj_t *messages = create_action_button(root, "MESSAGGI", FALAFEL_COLOR_NIGHT);
    lv_obj_align(messages, LV_ALIGN_BOTTOM_LEFT, 18, -15);
    messages_label = lv_obj_get_child(messages, 0);
    lv_obj_add_event_cb(messages, messages_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *write = create_action_button(root, "SCRIVI", FALAFEL_COLOR_YELLOW);
    lv_obj_align(write, LV_ALIGN_BOTTOM_RIGHT, -18, -15);
    lv_obj_add_event_cb(write, composer_clicked, LV_EVENT_CLICKED, NULL);

    add_pointer_handler_to_tree(root);
    falafel_home_refresh();
    return root;
}

void falafel_home_refresh(void)
{
    const personality_view_t *view = &personalities[falafel_gui.personality];
    lv_label_set_text(title_label, view->name);
    lv_label_set_text(line_label, view->line);
    lv_label_set_text(code_label, view->code);
    lv_image_set_src(art_image, view->image);

    for (int i = 0; i < FALAFEL_PERSONALITY_COUNT; ++i) {
        lv_obj_set_style_bg_color(markers[i], lv_color_hex(
            i == (int)falafel_gui.personality ? FALAFEL_COLOR_YELLOW : FALAFEL_COLOR_LIGHT_GRAY), 0);
    }

    if (falafel_gui.unread_count == 0) lv_label_set_text(messages_label, "MESSAGGI");
    else lv_label_set_text_fmt(messages_label, "MESSAGGI  %u", falafel_gui.unread_count);
}
