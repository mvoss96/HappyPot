#pragma once

#include <stdint.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>

#include "app_config.h"
#include "rolling_average.h"

struct SoilReading
{
	int32_t mv;
	int percent;
};

/** Capacitive soil moisture v2.0 probe. One instance per physical probe;
 * power pin and ADC channel come from devicetree, calibration per-probe.
 * Maintains its own rolling-average window over the last WINDOW_SIZE samples. */
class SoilSensor
{
public:
	SoilSensor(const adc_dt_spec &adc, const gpio_dt_spec &pwr,
			   int32_t dry_mv, int32_t wet_mv);

	int init();

	/** Take a fresh reading, push it into the rolling window, and return
	 * the smoothed value across all samples held so far via *out. */
	int sample(SoilReading *out);

private:
	int read_raw_mv(int32_t *mv_out);
	int mv_to_percent(int32_t mv) const;

	const adc_dt_spec &m_adc;  // ADC channel binding from devicetree
	const gpio_dt_spec &m_pwr; // probe VCC enable pin (active high)
	int32_t m_dry_mv;		   // calibration: reading in air -> 0%
	int32_t m_wet_mv;		   // calibration: reading submerged -> 100%

	RollingAverage<cfg::WINDOW_SIZE> m_avg; // smoothing over recent mV samples
};
