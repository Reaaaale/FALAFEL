#include "falafel_gui_internal.h"

/* Geometry stays in the circle's central chord. Fonts and palette are shared
 * with Home; no SDL, filesystem, transport or platform ownership lives here. */
lv_obj_t *falafel_page_create(lv_obj_t *parent, const char *title)
{
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *label = falafel_theme_create_label(root, title,
        &lv_font_montserrat_22, FALAFEL_COLOR_CREAM);
    lv_obj_set_width(label, LV_PCT(72));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_y(label, LV_PCT(16));
    return root;
}

lv_obj_t *falafel_page_button(lv_obj_t *parent, const char *text,
                              int32_t width_percent, int32_t y_percent)
{
    lv_obj_t *button = lv_button_create(parent);
    falafel_theme_style_button(button, FALAFEL_COLOR_SURFACE);
    lv_obj_set_size(button, LV_PCT(width_percent), LV_PCT(11));
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_align(button, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_y(button, LV_PCT(y_percent));
    lv_obj_t *label = falafel_theme_create_label(button, text,
        &lv_font_montserrat_16, FALAFEL_COLOR_CREAM);
    lv_obj_center(label);
    return button;
}

static void back_clicked(lv_event_t *event)
{
    (void)event;
    falafel_gui_go_back();
}

void falafel_page_back(lv_obj_t *parent, const char *text)
{
    lv_obj_t *back = falafel_page_button(parent, text, 44, 73);
    lv_obj_set_style_bg_color(back, lv_color_hex(FALAFEL_COLOR_ACCENT), 0);
    lv_obj_add_event_cb(back, back_clicked, LV_EVENT_CLICKED, NULL);
}

static void destination_clicked(lv_event_t *event)
{
    falafel_gui_navigate((falafel_gui_page_t)(uintptr_t)lv_event_get_user_data(event));
}

lv_obj_t *falafel_menu_create(lv_obj_t *parent)
{
    lv_obj_t *root = falafel_page_create(parent, "Menu");
    static const struct {
        const char *title;
        falafel_gui_page_t page;
    } entries[] = {
        {"Messaggi", FALAFEL_PAGE_MESSAGES},
        {"Giochi", FALAFEL_PAGE_GAMES},
        {"Impostazioni", FALAFEL_PAGE_SETTINGS},
    };
    for (unsigned i = 0; i < sizeof(entries) / sizeof(entries[0]); ++i) {
        lv_obj_t *button = falafel_page_button(root, entries[i].title, 58, 29 + 14 * i);
        lv_obj_add_event_cb(button, destination_clicked, LV_EVENT_CLICKED,
                           (void *)(uintptr_t)entries[i].page);
    }
    falafel_page_back(root, LV_SYMBOL_LEFT " Home");
    return root;
}

lv_obj_t *falafel_entry_create(lv_obj_t *parent, const char *title, const char *body)
{
    lv_obj_t *root = falafel_page_create(parent, title);
    lv_obj_t *label = falafel_theme_create_label(root, body,
        &lv_font_montserrat_16, FALAFEL_COLOR_CREAM);
    lv_obj_set_width(label, LV_PCT(64));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
    falafel_page_back(root, LV_SYMBOL_LEFT " Menu");
    return root;
}
