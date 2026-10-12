#include <stdio.h>
#include "falafel_gui_internal.h"

static lv_obj_t *wifi_label;
static lv_obj_t *peer_label;
static lv_obj_t *battery_label;
static lv_obj_t *wifi_dot;
static lv_obj_t *peer_dot;

static lv_obj_t *create_status_dot(lv_obj_t *parent, int32_t x)
{
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_set_size(dot, 9, 9);
    lv_obj_align(dot, LV_ALIGN_LEFT_MID, x, 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dot, 1, 0);
    lv_obj_set_style_border_color(dot, lv_color_hex(FALAFEL_COLOR_CREAM), 0);
    lv_obj_set_style_pad_all(dot, 0, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    return dot;
}

lv_obj_t *falafel_status_bar_create(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, 320, 48);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(FALAFEL_COLOR_NIGHT), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_hor(bar, 14, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *name = falafel_theme_create_label(bar, "FALAFEL OS", &lv_font_montserrat_16, FALAFEL_COLOR_CREAM);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);
    wifi_label = falafel_theme_create_label(bar, "W", &lv_font_montserrat_12, FALAFEL_COLOR_CREAM);
    lv_obj_align(wifi_label, LV_ALIGN_LEFT_MID, 135, 0);
    wifi_dot = create_status_dot(bar, 151);
    peer_label = falafel_theme_create_label(bar, "P", &lv_font_montserrat_12, FALAFEL_COLOR_CREAM);
    lv_obj_align(peer_label, LV_ALIGN_LEFT_MID, 176, 0);
    peer_dot = create_status_dot(bar, 192);
    battery_label = falafel_theme_create_label(bar, "--%", &lv_font_montserrat_12, FALAFEL_COLOR_CREAM);
    lv_obj_align(battery_label, LV_ALIGN_RIGHT_MID, 0, 0);
    falafel_status_bar_refresh();
    return bar;
}

void falafel_status_bar_refresh(void)
{
    char battery[8];
    lv_obj_set_style_bg_color(wifi_dot, lv_color_hex(
        falafel_gui.wifi_connected ? FALAFEL_COLOR_YELLOW : FALAFEL_COLOR_NIGHT), 0);
    lv_obj_set_style_bg_color(peer_dot, lv_color_hex(
        falafel_gui.peer_connected ? FALAFEL_COLOR_YELLOW : FALAFEL_COLOR_NIGHT), 0);
    (void)snprintf(battery, sizeof(battery), "%u%%", falafel_gui.battery_percent);
    lv_label_set_text(battery_label, battery);
}
