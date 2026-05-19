#pragma once

#include <zephyr/kernel.h>

namespace cfg {

/** Measurement interval. */
constexpr k_timeout_t MEASUREMENT_INTERVAL = K_SECONDS(60);

/** Display refreshes only when smoothed percent moves by at least this much. */
constexpr int DISPLAY_PCT_THRESHOLD = 1;

/** Rolling-average window. Smoothing horizon = WINDOW_SIZE * MEASUREMENT_INTERVAL. */
constexpr int WINDOW_SIZE = 10;

/** Capacitive soil probe calibration: measure DRY in air, WET submerged. */
constexpr int32_t SOIL_MV_DRY = 2700;
constexpr int32_t SOIL_MV_WET = 1500;

}  // namespace cfg
