#include "sensors/soil.hpp"

#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>

#include "app_config.hpp"
#include "util/rolling_average.hpp"

LOG_MODULE_REGISTER(soil, LOG_LEVEL_INF);

namespace
{
	/** Capacitive v2.0 probe has an onboard 555; let VCC settle before sampling. */
	constexpr int k_settle_ms = 50;

	const adc_dt_spec adc = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));
	const gpio_dt_spec pwr = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), soil_power_gpios);

	int32_t cal_dry_mv = cfg::SOIL_MV_DRY;
	int32_t cal_wet_mv = cfg::SOIL_MV_WET;

	RollingAverage<cfg::WINDOW_SIZE> avg; // smoothing over recent mV samples

	int read_raw_mv(int32_t *mv_out)
	{
		// power on the probe
		int err = gpio_pin_set_dt(&pwr, 1);
		if (err < 0)
			return err;

		// let the 555 timer settle and output a stable reading
		k_msleep(k_settle_ms);

		/** SAADC stays armed after a read unless we explicitly suspend it
		 * (~1.2 mA idle on nRF52840). Resume just for the sample, suspend
		 * straight after. -EALREADY is fine on either action. */
		pm_device_action_run(adc.dev, PM_DEVICE_ACTION_RESUME);

		int16_t raw = 0;
		adc_sequence seq{
			.buffer = &raw,
			.buffer_size = sizeof(raw),
		};
		err = adc_sequence_init_dt(&adc, &seq);
		if (err == 0)
			err = adc_read(adc.dev, &seq);

		pm_device_action_run(adc.dev, PM_DEVICE_ACTION_SUSPEND);

		// power off the probe to save energy and prevent corrosion
		gpio_pin_set_dt(&pwr, 0);
		if (err < 0)
			return err;

		int32_t mv = raw;
		err = adc_raw_to_millivolts_dt(&adc, &mv);
		if (err < 0)
			return err;

		*mv_out = mv;
		return 0;
	}

	int mv_to_percent(int32_t mv)
	{
		if (cal_dry_mv == cal_wet_mv)
			return 0;
		int pct = ((cal_dry_mv - mv) * 100) / (cal_dry_mv - cal_wet_mv);
		if (pct < 0)
			pct = 0;
		if (pct > 100)
			pct = 100;
		return pct;
	}

} // namespace

int soil::init()
{
	if (!adc_is_ready_dt(&adc))
	{
		LOG_ERR("ADC not ready");
		return -ENODEV;
	}
	int err = adc_channel_setup_dt(&adc);
	if (err < 0)
	{
		LOG_ERR("ADC channel setup failed (%d)", err);
		return err;
	}
	if (!gpio_is_ready_dt(&pwr))
	{
		LOG_ERR("Power GPIO not ready");
		return -ENODEV;
	}
	err = gpio_pin_configure_dt(&pwr, GPIO_OUTPUT_INACTIVE);
	if (err < 0)
	{
		LOG_ERR("Power GPIO configure failed (%d)", err);
		return err;
	}
	return 0;
}

int soil::sample_raw(int32_t *mv_out)
{
	return read_raw_mv(mv_out);
}

void soil::set_calibration(int32_t dry_mv, int32_t wet_mv)
{
	cal_dry_mv = dry_mv;
	cal_wet_mv = wet_mv;
}

int32_t soil::dry_mv()
{
	return cal_dry_mv;
}

int32_t soil::wet_mv()
{
	return cal_wet_mv;
}

int soil::sample(SoilReading *out)
{
	int32_t mv;
	if (auto err = read_raw_mv(&mv); err < 0)
	{
		return err;
	}

	LOG_INF("raw %4d mV  ~%3d %%", mv, mv_to_percent(mv));

	int32_t avg_mv = avg.push(mv);

	out->mv = avg_mv;
	out->percent = mv_to_percent(avg_mv);
	return 0;
}
