#pragma once
#define LV_CONF_H

/* Minimal project-specific LVGL configuration. Unspecified options use LVGL's
 * defaults. This file is shared by all simulator compilation units. */
#define LV_COLOR_DEPTH 32
#define LV_USE_SDL 1
#define LV_SDL_INCLUDE_PATH <SDL2/SDL.h>
#define LV_USE_LODEPNG 1

#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_22 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_12 1
