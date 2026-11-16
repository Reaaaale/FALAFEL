#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "falafel_gui.h"
#include "lvgl.h"

/* Flat sage/cream/toasted-orange simulator palette. Legacy names keep the
 * existing GUI widgets on the same theme without changing their behavior. */
#define FALAFEL_COLOR_SAGE       0x34483E
#define FALAFEL_COLOR_ACCENT     0xD88A35
#define FALAFEL_COLOR_SURFACE    0x40564A
#define FALAFEL_COLOR_MUTED      0x718276
#define FALAFEL_COLOR_CREAM      0xF3E9D8
#define FALAFEL_COLOR_HORN_BLUE  FALAFEL_COLOR_SURFACE
#define FALAFEL_COLOR_YELLOW     FALAFEL_COLOR_ACCENT
#define FALAFEL_COLOR_NIGHT      FALAFEL_COLOR_SAGE
#define FALAFEL_COLOR_LIGHT_GRAY FALAFEL_COLOR_MUTED
#define FALAFEL_COLOR_PALE       FALAFEL_COLOR_SURFACE
#define FALAFEL_COLOR_WHITE      FALAFEL_COLOR_CREAM

#define FALAFEL_ROTATION_FRAME_COUNT 24
#define FALAFEL_ROTATION_DURATION_MS 280
#define FALAFEL_ROTATION_BACK_FRAME 12
extern const lv_image_dsc_t falafel_asset_rotation[FALAFEL_PERSONALITY_COUNT][FALAFEL_ROTATION_FRAME_COUNT];
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
    lv_obj_t *menu;
    lv_obj_t *games;
    lv_obj_t *settings;
    falafel_gui_page_t page;
    falafel_gui_page_t messages_parent;
    lv_obj_t *notification;
    lv_obj_t *notification_label;
    lv_timer_t *notification_timer;
    falafel_gui_callbacks_t callbacks;
    falafel_personality_t personality;
    bool personality_animating;
    uint8_t rotation_frame;
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

/* Shared circular-page primitives. New views size themselves from their parent. */
lv_obj_t *falafel_page_create(lv_obj_t *parent, const char *title);
lv_obj_t *falafel_page_button(lv_obj_t *parent, const char *text,
                              int32_t width_percent, int32_t y_percent);
void falafel_page_back(lv_obj_t *parent, const char *text);
lv_obj_t *falafel_menu_create(lv_obj_t *parent);
lv_obj_t *falafel_entry_create(lv_obj_t *parent, const char *title, const char *body);
