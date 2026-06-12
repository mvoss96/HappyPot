#pragma once

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include "soil.h"

/**
 * Initialise the calibration subsystem.
 *
 * Loads any previously stored calibration from NVS into @p soil, then arms
 * the button hold-to-calibrate state machine:
 *
 *   Hold 0–2 s  → release → WET calibration  (sample 5 s, save)
 *   Hold 2–4 s  → release → DRY calibration  (sample 5 s, save)
 *   Hold 4 s+   → release → RESET to compile-time defaults
 *
 * @param soil         Probe instance whose calibration points are updated.
 * @param btn          Button GPIO spec (ACTIVE_LOW | PULL_UP assumed).
 * @param resume_work  Delayable work rescheduled after calibration finishes
 *                     (the caller's periodic measurement work).
 * @return 0 on success, negative errno on failure.
 */
int calib_init(SoilSensor &soil, const gpio_dt_spec &btn,
               k_work_delayable *resume_work);
