#pragma once
#include "lvgl.h"
#include "SDL2/SDL.h"
/* Returns 0 while running, 1 success, -1 failure. Drives actual SDL input. */
int ui_smoke_step(lv_display_t *display, SDL_Window *window);
