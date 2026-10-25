#include "falafel_gui_internal.h"

static lv_obj_t *textarea, *keyboard;
static void back_clicked(lv_event_t *event) { (void)event; falafel_gui_show_home(); }
static void send_clicked(lv_event_t *event)
{
    (void)event;
    const char *text = lv_textarea_get_text(textarea);
    if (text == NULL || text[0] == '\0') { falafel_gui_show_notification("Scrivi prima un messaggio"); return; }
    if (falafel_gui.callbacks.on_send_message == NULL ||
        !falafel_gui.callbacks.on_send_message(text, falafel_gui.callbacks.user_data)) {
        falafel_gui_show_notification("Invio non riuscito");
        return;
    }
    falafel_gui_add_sent_message(text); lv_textarea_set_text(textarea, "");
    falafel_gui_show_messages(); falafel_gui_show_notification("Messaggio in coda");
}

lv_obj_t *falafel_composer_create(lv_obj_t *parent)
{
    lv_obj_t *root = lv_obj_create(parent); lv_obj_set_size(root, 320, 432);
    lv_obj_align(root, LV_ALIGN_BOTTOM_MID, 0, 0); lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(root, 0, 0); lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *back = lv_button_create(root); lv_obj_set_size(back, 42, 40);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 14, 10); falafel_theme_style_button(back, FALAFEL_COLOR_CREAM);
    lv_obj_t *back_label = falafel_theme_create_label(back, "<", &lv_font_montserrat_18, FALAFEL_COLOR_NIGHT);
    lv_obj_center(back_label); lv_obj_add_event_cb(back, back_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *title = falafel_theme_create_label(root, "SCRIVI", &lv_font_montserrat_22, FALAFEL_COLOR_NIGHT);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 70, 17);
    lv_obj_t *send = lv_button_create(root); lv_obj_set_size(send, 72, 40);
    lv_obj_align(send, LV_ALIGN_TOP_RIGHT, -14, 10); falafel_theme_style_button(send, FALAFEL_COLOR_YELLOW);
    lv_obj_t *send_label = falafel_theme_create_label(send, "INVIA", &lv_font_montserrat_14, FALAFEL_COLOR_NIGHT);
    lv_obj_center(send_label); lv_obj_add_event_cb(send, send_clicked, LV_EVENT_CLICKED, NULL);
    textarea = lv_textarea_create(root); lv_obj_set_size(textarea, 300, 82);
    lv_obj_align(textarea, LV_ALIGN_TOP_MID, 0, 60); lv_textarea_set_placeholder_text(textarea, "Il tuo messaggio...");
    lv_textarea_set_max_length(textarea, 240); lv_obj_set_style_bg_color(textarea, lv_color_hex(FALAFEL_COLOR_WHITE), 0);
    lv_obj_set_style_text_color(textarea, lv_color_hex(FALAFEL_COLOR_NIGHT), 0);
    lv_obj_set_style_border_color(textarea, lv_color_hex(FALAFEL_COLOR_NIGHT), 0);
    lv_obj_set_style_border_width(textarea, 2, 0); lv_obj_set_style_radius(textarea, 8, 0);
    keyboard = lv_keyboard_create(root); lv_obj_set_size(keyboard, 320, 274);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0); lv_keyboard_set_textarea(keyboard, textarea);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(FALAFEL_COLOR_LIGHT_GRAY), 0);
    lv_obj_set_style_radius(keyboard, 0, 0); lv_obj_set_style_border_width(keyboard, 0, 0);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(FALAFEL_COLOR_CREAM), LV_PART_ITEMS);
    lv_obj_set_style_text_color(keyboard, lv_color_hex(FALAFEL_COLOR_NIGHT), LV_PART_ITEMS);
    lv_obj_set_style_border_color(keyboard, lv_color_hex(FALAFEL_COLOR_NIGHT), LV_PART_ITEMS);
    lv_obj_set_style_border_width(keyboard, 1, LV_PART_ITEMS);
    lv_obj_set_style_radius(keyboard, 5, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(FALAFEL_COLOR_YELLOW), LV_PART_ITEMS | LV_STATE_PRESSED);
    return root;
}

void falafel_composer_reset(void) { lv_textarea_set_text(textarea, ""); lv_keyboard_set_textarea(keyboard, textarea); }
