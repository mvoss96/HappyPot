#pragma once

#include <stdint.h>

int ui_init(void);

/** Repaint with the latest reading. No-op if neither the percent nor the
 * resolved icon has changed. */
int ui_show_reading(int32_t mv, int percent);

/** Show dedicated low-battery warning screen. */
int ui_show_low_battery(int battery_percent);

/** Show a calibration step label with an optional captured mV value.
 *  The next call to ui_show_reading() restores the normal view. */
void ui_show_calibration_step(const char *label, int32_t mv);
void ui_show_calibration_step(const char *label);

/** Immediately restore the normal reading screen (used when calibration is
 *  cancelled while still held). */
void ui_exit_calibration();
