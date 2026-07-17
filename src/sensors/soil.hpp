#pragma once

#include <stdint.h>

/** One smoothed reading of the probe. */
struct SoilReading
{
	int32_t mv;
	int percent;
};

/** @file
 * Capacitive soil moisture v2.0 probe. The module owns its hardware -- the ADC channel and the
 * probe's power-gate GPIO come from the devicetree (zephyr,user), like every sensor here -- and
 * its calibration: two mV endpoints (dry in air, wet submerged) that prefs pushes in on boot and
 * whenever the user recalibrates.
 *
 * The probe is only powered while sampling: ~5 mA otherwise continuous, and a permanently biased
 * electrode corrodes.
 */
namespace soil
{
	/** Bring up the ADC channel and the power gate.
	 * @retval 0 ready; negative = no probe on this board */
	int init();

	/** Take a fresh reading, push it into the rolling window, and return the smoothed value
	 * across all samples held so far via *out. Blocks ~50 ms (probe settle).
	 * @retval 0 ok; negative = ADC error */
	int sample(SoilReading *out);

	/** One-shot raw reading without touching the rolling average. Used during calibration to
	 * capture the probe voltage. Blocks ~50 ms.
	 * @retval 0 ok; negative = ADC error */
	int sample_raw(int32_t *mv_out);

	/** Replace the calibration endpoints. prefs::apply_all() is the usual caller. */
	void set_calibration(int32_t dry_mv, int32_t wet_mv);
	int32_t dry_mv();
	int32_t wet_mv();
}
