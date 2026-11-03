#pragma once
#define LV_CONF_H

/* Minimal project-specific LVGL configuration. Unspecified options use LVGL's
 * defaults. This file is shared by all simulator compilation units. */
#define LV_COLOR_DEPTH 32
#define LV_USE_SDL 1
#define LV_SDL_DIRECT_EXIT 0
#define LV_SDL_INCLUDE_PATH <SDL2/SDL.h>
#define LV_USE_LODEPNG 1
/* Decode only the currently drawn PNG; never retain the 24 decoded frames. */
#define LV_CACHE_DEF_SIZE 0
/* Two refresh opportunities per 30ms rotation frame. */
#define LV_DEF_REFR_PERIOD 15

#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_22 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_12 1

/* Desktop budget for the 466px PNG decode and circular clipping layer. */
#define LV_MEM_SIZE (8 * 1024 * 1024U)
#define LV_USE_SNAPSHOT 1
