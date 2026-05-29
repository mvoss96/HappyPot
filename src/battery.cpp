#include "battery.h"

#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(battery, LOG_LEVEL_INF);

namespace
{
	struct BatteryPoint
	{
		int32_t mv;
		int pct;
	};

	/** Typical single-cell Li-Ion open-circuit curve, piecewise-linearized. */
	constexpr BatteryPoint kCurve[] = {
		{3300, 0},
		{3600, 5},
		{3680, 10},
		{3740, 20},
		{3770, 30},
		{3790, 40},
		{3820, 50},
		{3870, 60},
		{3920, 70},
		{4000, 80},
		{4110, 90},
		{4200, 100},
	};
}

BatteryMonitor::BatteryMonitor(const adc_dt_spec &adc)
	: m_adc(adc)
{
}

int BatteryMonitor::init()
{
	if (!adc_is_ready_dt(&m_adc))
	{
		LOG_ERR("Battery ADC not ready");
		return -ENODEV;
	}

	const int err = adc_channel_setup_dt(&m_adc);
	if (err < 0)
	{
		LOG_ERR("Battery ADC channel setup failed (%d)", err);
		return err;
	}

	return 0;
}

int BatteryMonitor::read_raw_mv(int32_t *mv_out)
{
	pm_device_action_run(m_adc.dev, PM_DEVICE_ACTION_RESUME);

	int16_t raw = 0;
	adc_sequence seq{
		.buffer = &raw,
		.buffer_size = sizeof(raw),
	};
	int err = adc_sequence_init_dt(&m_adc, &seq);
	if (err == 0)
	{
		err = adc_read(m_adc.dev, &seq);
	}

	pm_device_action_run(m_adc.dev, PM_DEVICE_ACTION_SUSPEND);
	if (err < 0)
	{
		return err;
	}

	int32_t mv = raw;
	err = adc_raw_to_millivolts_dt(&m_adc, &mv);
	if (err < 0)
	{
		return err;
	}

	/* Internal SAADC channel provides VDDH/5, so scale back to cell voltage. */
	mv *= 5;

	*mv_out = mv;
	return 0;
}

int BatteryMonitor::mv_to_percent(int32_t mv) const
{
	if (mv <= kCurve[0].mv)
	{
		return 0;
	}
	if (mv >= kCurve[ARRAY_SIZE(kCurve) - 1].mv)
	{
		return 100;
	}

	for (size_t i = 1; i < ARRAY_SIZE(kCurve); ++i)
	{
		if (mv <= kCurve[i].mv)
		{
			const BatteryPoint lo = kCurve[i - 1];
			const BatteryPoint hi = kCurve[i];
			const int32_t num = (mv - lo.mv) * (hi.pct - lo.pct);
			const int32_t den = (hi.mv - lo.mv);
			return lo.pct + static_cast<int>(num / den);
		}
	}

	return 100;
}

int BatteryMonitor::sample(BatteryReading *out)
{
	int32_t mv = 0;
	if (const int err = read_raw_mv(&mv); err < 0)
	{
		return err;
	}

	const int32_t avg_mv = m_avg.push(mv);
	out->mv = avg_mv;
	out->percent = mv_to_percent(avg_mv);
	return 0;
}
