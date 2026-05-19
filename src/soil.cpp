#include "soil.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(soil, LOG_LEVEL_INF);

namespace
{
	/** Capacitive v2.0 probe has an onboard 555; let VCC settle before sampling. */
	constexpr int k_settle_ms = 50;

} // namespace

SoilSensor::SoilSensor(const adc_dt_spec &adc, const gpio_dt_spec &pwr,
					   int32_t dry_mv, int32_t wet_mv)
	: m_adc(adc), m_pwr(pwr), m_dry_mv(dry_mv), m_wet_mv(wet_mv)
{
}

int SoilSensor::init()
{
	if (!adc_is_ready_dt(&m_adc))
	{
		LOG_ERR("ADC not ready");
		return -ENODEV;
	}
	int err = adc_channel_setup_dt(&m_adc);
	if (err < 0)
	{
		LOG_ERR("ADC channel setup failed (%d)", err);
		return err;
	}
	if (!gpio_is_ready_dt(&m_pwr))
	{
		LOG_ERR("Power GPIO not ready");
		return -ENODEV;
	}
	err = gpio_pin_configure_dt(&m_pwr, GPIO_OUTPUT_INACTIVE);
	if (err < 0)
	{
		LOG_ERR("Power GPIO configure failed (%d)", err);
		return err;
	}
	return 0;
}

int SoilSensor::read_raw_mv(int32_t *mv_out)
{
	// power on the probe
	int err = gpio_pin_set_dt(&m_pwr, 1);
	if (err < 0)
		return err;

	// let the 555 timer settle and output a stable reading
	k_msleep(k_settle_ms);

	int16_t raw = 0;
	adc_sequence seq{
		.buffer = &raw,
		.buffer_size = sizeof(raw),
	};
	err = adc_sequence_init_dt(&m_adc, &seq);
	if (err == 0)
		err = adc_read(m_adc.dev, &seq);

	// power off the probe to save energy and prevent corrosion
	gpio_pin_set_dt(&m_pwr, 0);
	if (err < 0)
		return err;

	int32_t mv = raw;
	err = adc_raw_to_millivolts_dt(&m_adc, &mv);
	if (err < 0)
		return err;

	*mv_out = mv;
	return 0;
}

int SoilSensor::mv_to_percent(int32_t mv) const
{
	int pct = ((m_dry_mv - mv) * 100) / (m_dry_mv - m_wet_mv);
	if (pct < 0)
		pct = 0;
	if (pct > 100)
		pct = 100;
	return pct;
}

int SoilSensor::sample(SoilReading *out)
{
	int32_t mv;
	if (auto err = read_raw_mv(&mv); err < 0)
	{
		return err;
	}

	LOG_INF("raw %4d mV  ~%3d %%", mv, mv_to_percent(mv));

	int32_t avg_mv = m_avg.push(mv);

	out->mv = avg_mv;
	out->percent = mv_to_percent(avg_mv);
	return 0;
}
