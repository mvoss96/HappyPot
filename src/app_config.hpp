#pragma once

#include <stdint.h>

namespace cfg
{
    /** Measurement cadence, in milliseconds (the loop's TICK). */
    constexpr int64_t MEASUREMENT_MS = 10000;

    /** Rolling-average window. Smoothing horizon = WINDOW_SIZE * MEASUREMENT_MS. */
    constexpr int WINDOW_SIZE = 10;

    /** Capacitive soil probe calibration: measure DRY in air, WET submerged. */
    constexpr int32_t SOIL_MV_DRY = 2000;
    constexpr int32_t SOIL_MV_WET = 1500;

    /** Battery smoothing over the last N samples. */
    constexpr int BATTERY_WINDOW_SIZE = 6;

    /** Show dedicated low-battery warning screen at or below this percentage. */
    constexpr int LOW_BATTERY_PCT = 5;

    /** Mood thresholds. percent >= HAPPY_PCT -> happy; >= MEH_PCT -> meh;
     * below -> thirsty. */
    constexpr int MOOD_HAPPY_PCT = 60;
    constexpr int MOOD_MEH_PCT = 30;

    /* The firmware version lives in <repo>/VERSION -- see src/version.hpp. */

} // namespace cfg
