#pragma once

#include <stdbool.h>
#include <stdint.h>

void trackpad_navigation_init(uint32_t window_id, bool debug_logging);
void trackpad_navigation_process(void);
void trackpad_navigation_deinit(void);

