#include "falafel_gui_internal.h"

void falafel_theme_apply_screen(lv_obj_t *screen)
{
    lv_obj_set_style_bg_color(screen, lv_color_hex(FALAFEL_COLOR_CREAM), 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(FALAFEL_COLOR_NIGHT), 0);
    lv_obj_set_style_text_font(screen, &lv_font_montserrat_16, 0);
}

void falafel_theme_style_button(lv_obj_t *button, uint32_t color)
{
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_border_width(button, 2, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(FALAFEL_COLOR_NIGHT), 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_text_color(button, lv_color_hex(FALAFEL_COLOR_NIGHT), 0);
    lv_obj_set_style_pad_hor(button, 18, 0);
    lv_obj_set_style_translate_y(button, 2, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_80, LV_STATE_PRESSED);
}

lv_obj_t *falafel_theme_create_label(lv_obj_t *parent, const char *text,
                                     const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}
