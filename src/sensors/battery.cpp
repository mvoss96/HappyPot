#include "sensors/battery.hpp"

#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>

#include "app_config.hpp"
#include "sensors/battery_curve.hpp"
#include "util/rolling_average.hpp"

LOG_MODULE_REGISTER(battery, LOG_LEVEL_INF);

namespace
{
	const adc_dt_spec adc = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 1);

	RollingAverage<cfg::BATTERY_WINDOW_SIZE> avg;

	int read_raw_mv(int32_t *mv_out)
	{
		pm_device_action_run(adc.dev, PM_DEVICE_ACTION_RESUME);

		int16_t raw = 0;
		adc_sequence seq{
			.buffer = &raw,
			.buffer_size = sizeof(raw),
		};
		int err = adc_sequence_init_dt(&adc, &seq);
		if (err == 0)
		{
			err = adc_read(adc.dev, &seq);
		}

		pm_device_action_run(adc.dev, PM_DEVICE_ACTION_SUSPEND);
		if (err < 0)
		{
			return err;
		}

		int32_t mv = raw;
		err = adc_raw_to_millivolts_dt(&adc, &mv);
		if (err < 0)
		{
			return err;
		}

		/* Internal SAADC channel provides VDDH/5, so scale back to cell voltage. */
		mv *= 5;

		*mv_out = mv;
		return 0;
	}

} // namespace

int battery::init()
{
	if (!adc_is_ready_dt(&adc))
	{
		LOG_ERR("Battery ADC not ready");
		return -ENODEV;
	}

	const int err = adc_channel_setup_dt(&adc);
	if (err < 0)
	{
		LOG_ERR("Battery ADC channel setup failed (%d)", err);
		return err;
	}

	return 0;
}

int battery::sample(BatteryReading *out)
{
	int32_t mv = 0;
	if (const int err = read_raw_mv(&mv); err < 0)
	{
		return err;
	}

	const int32_t avg_mv = avg.push(mv);
	out->mv = avg_mv;
	out->percent = battery_curve::mv_to_percent(avg_mv);
	return 0;
}
