#pragma once

#include <zephyr/kernel.h>

namespace cfg {

/** Measurement interval. */
constexpr k_timeout_t MEASUREMENT_INTERVAL = K_SECONDS(10);

/** Rolling-average window. Smoothing horizon = WINDOW_SIZE * MEASUREMENT_INTERVAL. */
constexpr int WINDOW_SIZE = 10;

/** Capacitive soil probe calibration: measure DRY in air, WET submerged. */
constexpr int32_t SOIL_MV_DRY = 2000;
constexpr int32_t SOIL_MV_WET = 1500;

/** Mood thresholds. percent >= HAPPY_PCT -> happy; >= MEH_PCT -> meh;
 * below -> thirsty. */
constexpr int MOOD_HAPPY_PCT = 60;
constexpr int MOOD_MEH_PCT = 30;

}  // namespace cfg
