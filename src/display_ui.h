#pragma once

#include <stdint.h>

int ui_init(void);

/** Repaint with the latest reading. No-op if neither the percent nor the
 * resolved icon has changed. */
int ui_show_reading(int32_t mv, int percent);

/** Show dedicated low-battery warning screen. */
int ui_show_low_battery(int battery_percent);
