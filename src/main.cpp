#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/pm/device.h>
#include <zephyr/logging/log.h>

#include "app_config.h"
#include "battery.h"
#include "bthome_adv.h"
#include "display_ui.h"
#include "soil.h"

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

namespace
{
    const adc_dt_spec soil0_adc = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));
    const adc_dt_spec battery_adc = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 1);
    const gpio_dt_spec soil0_pwr = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), soil_power_gpios);
    const struct device *const log_uart = DEVICE_DT_GET(DT_NODELABEL(uart1));

    SoilSensor soil(soil0_adc, soil0_pwr, cfg::SOIL_MV_DRY, cfg::SOIL_MV_WET);
    BatteryMonitor battery(battery_adc);

    void set_log_uart_sleep(bool sleep)
    {
        if (!device_is_ready(log_uart))
        {
            return;
        }
        const pm_device_action action = sleep ? PM_DEVICE_ACTION_SUSPEND : PM_DEVICE_ACTION_RESUME;
        (void)pm_device_action_run(log_uart, action);
    }

    void measurement_cycle(struct k_work *work);
    K_WORK_DELAYABLE_DEFINE(measurement_work, measurement_cycle);

    void sample_and_publish()
    {
        SoilReading soil_reading;
        if (auto err = soil.sample(&soil_reading); err < 0)
        {
            LOG_ERR("soil sample failed (%d)", err);
            return;
        }

        BatteryReading battery_reading;
        if (auto err = battery.sample(&battery_reading); err < 0)
        {
            LOG_ERR("battery sample failed (%d)", err);
            return;
        }

        LOG_INF("soil %4d mV  ~%3d %% | batt %4d mV  ~%3d %%",
                soil_reading.mv,
                soil_reading.percent,
                battery_reading.mv,
                battery_reading.percent);
        if (battery_reading.percent <= cfg::LOW_BATTERY_PCT)
        {
            ui_show_low_battery(battery_reading.percent);
        }
        else
        {
            ui_show_reading(soil_reading.mv, soil_reading.percent);
        }
        bthome_publish(soil_reading.percent, battery_reading.mv, battery_reading.percent);
    }

    void measurement_cycle(struct k_work *)
    {
        set_log_uart_sleep(false);
        sample_and_publish();
        set_log_uart_sleep(true);
        k_work_reschedule(&measurement_work, cfg::MEASUREMENT_INTERVAL);
    }

} // namespace

int main()
{
    set_log_uart_sleep(false);
    LOG_INF("UART1 TX test active on P1.06 @115200");

    if (soil.init() < 0)
    {
        LOG_ERR("soil init failed");
        return 0;
    }
    if (battery.init() < 0)
    {
        LOG_ERR("battery init failed");
        return 0;
    }
    if (ui_init() < 0)
    {
        LOG_ERR("ui init failed");
        return 0;
    }
    if (bthome_init() < 0)
    {
        LOG_ERR("bthome init failed");
        return 0;
    }

    /** Let the boot splash finish rendering (~3 s for a full SSD1681
     * refresh) before the first sample replaces it. */
    k_work_schedule(&measurement_work, K_SECONDS(4));
    set_log_uart_sleep(true);
    return 0;
}
