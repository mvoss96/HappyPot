#pragma once

#include <stdint.h>

int ui_init(void);

/** Repaint with the latest reading. No-op if the shown percent has not moved
 * by at least cfg::DISPLAY_PCT_THRESHOLD since the last successful repaint. */
int ui_show_reading(int32_t mv, int percent);
