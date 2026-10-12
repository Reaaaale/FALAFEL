#include "falafel_gui_internal.h"

static lv_obj_t *message_list;

static void back_clicked(lv_event_t *event) { (void)event; falafel_gui_show_home(); }
static void compose_clicked(lv_event_t *event) { (void)event; falafel_gui_show_composer(); }

static lv_obj_t *header_button(lv_obj_t *parent, const char *text, int32_t width, uint32_t color)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, width, 40);
    falafel_theme_style_button(button, color);
    lv_obj_t *label = falafel_theme_create_label(button, text, &lv_font_montserrat_14,
                                                  color == FALAFEL_COLOR_NIGHT
                                                      ? FALAFEL_COLOR_CREAM
                                                      : FALAFEL_COLOR_NIGHT);
    lv_obj_center(label);
    return button;
}

lv_obj_t *falafel_messages_create(lv_obj_t *parent)
{
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_size(root, 320, 432);
    lv_obj_align(root, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = header_button(root, "<", 42, FALAFEL_COLOR_CREAM);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 14, 10);
    lv_obj_add_event_cb(back, back_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *title = falafel_theme_create_label(root, "MESSAGGI", &lv_font_montserrat_22,
                                                 FALAFEL_COLOR_NIGHT);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 70, 17);

    message_list = lv_obj_create(root);
    lv_obj_set_size(message_list, 292, 302);
    lv_obj_align(message_list, LV_ALIGN_TOP_MID, 0, 62);
    lv_obj_set_style_bg_color(message_list, lv_color_hex(FALAFEL_COLOR_LIGHT_GRAY), 0);
    lv_obj_set_style_border_color(message_list, lv_color_hex(FALAFEL_COLOR_NIGHT), 0);
    lv_obj_set_style_border_width(message_list, 2, 0);
    lv_obj_set_style_radius(message_list, 8, 0);
    lv_obj_set_style_pad_all(message_list, 10, 0);
    lv_obj_set_style_pad_row(message_list, 8, 0);
    lv_obj_set_flex_flow(message_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(message_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(message_list, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_bg_color(message_list, lv_color_hex(FALAFEL_COLOR_HORN_BLUE), LV_PART_SCROLLBAR);

    lv_obj_t *compose = header_button(root, "NUOVO MESSAGGIO", 190, FALAFEL_COLOR_YELLOW);
    lv_obj_set_height(compose, 48);
    lv_obj_align(compose, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_event_cb(compose, compose_clicked, LV_EVENT_CLICKED, NULL);
    return root;
}

void falafel_messages_add(const char *text, bool sent)
{
    if (message_list == NULL || text == NULL || text[0] == '\0') return;
    lv_obj_t *row = lv_obj_create(message_list);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *message = lv_obj_create(row);
    lv_obj_set_width(message, 220);
    lv_obj_set_height(message, LV_SIZE_CONTENT);
    lv_obj_align(message, sent ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(message, lv_color_hex(
        sent ? FALAFEL_COLOR_HORN_BLUE : FALAFEL_COLOR_PALE), 0);
    lv_obj_set_style_border_color(message, lv_color_hex(FALAFEL_COLOR_NIGHT), 0);
    lv_obj_set_style_border_width(message, 2, 0);
    lv_obj_set_style_radius(message, 6, 0);
    lv_obj_set_style_pad_all(message, 11, 0);
    lv_obj_clear_flag(message, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = falafel_theme_create_label(message, text, &lv_font_montserrat_14,
                                                  sent ? FALAFEL_COLOR_WHITE : FALAFEL_COLOR_NIGHT);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, 194);
    lv_obj_set_height(label, LV_SIZE_CONTENT);
    lv_obj_update_layout(label);
    lv_obj_set_height(message, lv_obj_get_height(label) + 22);
    lv_obj_set_height(row, lv_obj_get_height(message));
    lv_obj_scroll_to_view(row, LV_ANIM_ON);
}
