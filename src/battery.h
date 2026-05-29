#pragma once

#include <stdint.h>
#include <zephyr/drivers/adc.h>

#include "app_config.h"
#include "rolling_average.h"

struct BatteryReading
{
	int32_t mv;
	int percent;
};

/** Battery monitor based on a dedicated ADC channel. */
class BatteryMonitor
{
public:
	explicit BatteryMonitor(const adc_dt_spec &adc);

	int init();
	int sample(BatteryReading *out);

private:
	int read_raw_mv(int32_t *mv_out);
	int mv_to_percent(int32_t mv) const;

	const adc_dt_spec &m_adc;
	RollingAverage<cfg::BATTERY_WINDOW_SIZE> m_avg;
};
