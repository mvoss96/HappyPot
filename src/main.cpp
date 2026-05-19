#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app_config.h"
#include "display_ui.h"
#include "soil.h"

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

namespace
{
	const adc_dt_spec soil0_adc = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));
	const gpio_dt_spec soil0_pwr = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), soil_power_gpios);
	
	SoilSensor soil(soil0_adc, soil0_pwr, cfg::SOIL_MV_DRY, cfg::SOIL_MV_WET);

	void measurement_cycle(struct k_work *work);
	K_WORK_DELAYABLE_DEFINE(measurement_work, measurement_cycle);

	void sample_and_publish()
	{
		SoilReading reading;
		if (auto err = soil.sample(&reading); err < 0)
		{
			LOG_ERR("soil sample failed (%d)", err);
			return;
		}
		LOG_INF("reading %4d mV  ~%3d %%", reading.mv, reading.percent);
		ui_show_reading(reading.mv, reading.percent);
	}

	void measurement_cycle(struct k_work *)
	{
		sample_and_publish();
		k_work_reschedule(&measurement_work, cfg::MEASUREMENT_INTERVAL);
	}

} // namespace

int main()
{
	if (soil.init() < 0)
	{
		LOG_ERR("soil init failed");
		return 0;
	}
	if (ui_init() < 0)
	{
		LOG_ERR("ui init failed");
		return 0;
	}

	/** Let the boot splash finish rendering (~3 s for a full SSD1681
	 * refresh) before the first sample replaces it. */
	k_work_schedule(&measurement_work, K_SECONDS(4));
	return 0;
}
