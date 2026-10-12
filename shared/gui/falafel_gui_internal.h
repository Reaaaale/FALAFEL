#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "falafel_gui.h"
#include "lvgl.h"

#define FALAFEL_COLOR_HORN_BLUE  0x2455A4
#define FALAFEL_COLOR_YELLOW     0xF5C518
#define FALAFEL_COLOR_CREAM      0xFFF9EB
#define FALAFEL_COLOR_NIGHT      0x17243A
#define FALAFEL_COLOR_LIGHT_GRAY 0xE7EAF0
#define FALAFEL_COLOR_PALE       0xFFF3B5
#define FALAFEL_COLOR_WHITE      0xFFFFFF

extern const lv_image_dsc_t falafel_asset_taxi;
extern const lv_image_dsc_t falafel_asset_cook;
extern const lv_image_dsc_t falafel_asset_reminder;

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *status_bar;
    lv_obj_t *content;
    lv_obj_t *home;
    lv_obj_t *messages;
    lv_obj_t *composer;
    lv_obj_t *notification;
    lv_obj_t *notification_label;
    lv_timer_t *notification_timer;
    falafel_gui_callbacks_t callbacks;
    falafel_personality_t personality;
    bool wifi_connected;
    bool peer_connected;
    uint8_t battery_percent;
    uint16_t unread_count;
} falafel_gui_state_t;

extern falafel_gui_state_t falafel_gui;

void falafel_theme_apply_screen(lv_obj_t *screen);
void falafel_theme_style_button(lv_obj_t *button, uint32_t color);
lv_obj_t *falafel_theme_create_label(lv_obj_t *parent, const char *text,
                                     const lv_font_t *font, uint32_t color);
lv_obj_t *falafel_status_bar_create(lv_obj_t *parent);
void falafel_status_bar_refresh(void);
lv_obj_t *falafel_home_create(lv_obj_t *parent);
void falafel_home_refresh(void);
lv_obj_t *falafel_messages_create(lv_obj_t *parent);
void falafel_messages_add(const char *text, bool sent);
lv_obj_t *falafel_composer_create(lv_obj_t *parent);
void falafel_composer_reset(void);
void falafel_gui_show_home(void);
void falafel_gui_show_messages(void);
void falafel_gui_show_composer(void);
