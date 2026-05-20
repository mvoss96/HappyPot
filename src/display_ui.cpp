#include "display_ui.h"

#include <stdio.h>
#include <lvgl.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>

#include "app_config.h"
#include "ui_images/ui_images.h"

LOG_MODULE_REGISTER(ui, LOG_LEVEL_INF);

namespace
{
	const device *const display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	const device *const ext_3v3 = DEVICE_DT_GET(DT_NODELABEL(ext_3v3));

	/** SPI peripheral feeding the panel. Suspending it applies the spi2_sleep
	 * pinctrl (low-power-enable) so SCK/MOSI go hi-Z while the rail is down. */
	const device *const panel_spi = DEVICE_DT_GET(DT_NODELABEL(spi2));

	/** Panel control lines. With the 3V3 rail off, any of these driven high
	 * back-feeds the SSD1681 through its ESD diodes (~15 mA). We park all three
	 * physically LOW before cutting power; the drivers re-drive them on resume.
	 * Logical level chosen so the *physical* pin is low for each polarity:
	 * DC is active-high (0 -> low); RST and CS are active-low (1 -> low). */
	const gpio_dt_spec dc_pin = GPIO_DT_SPEC_GET(DT_NODELABEL(mipi_dbi), dc_gpios);
	const gpio_dt_spec rst_pin = GPIO_DT_SPEC_GET(DT_NODELABEL(mipi_dbi), reset_gpios);
	const gpio_dt_spec cs_pin = GPIO_DT_SPEC_GET_BY_IDX(DT_NODELABEL(spi2), cs_gpios, 0);

	lv_obj_t *icon;
	lv_obj_t *pct_label;

	unsigned design = 1;
	int last_shown_percent = -1;
	const lv_image_dsc_t *last_icon_src = nullptr;
	
	/** [design][mood] -> image. Mood order: 0=thirsty, 1=meh, 2=happy. */
	const lv_image_dsc_t *const icon_table[2][3] = {
		{&s0_thirsty, &s0_meh, &s0_happy},
		{&s1_thirsty, &s1_meh, &s1_happy},
	};

	const lv_image_dsc_t *icon_for_percent(int percent)
	{
		int mood = (percent >= cfg::MOOD_HAPPY_PCT) ? 2
				   : (percent >= cfg::MOOD_MEH_PCT) ? 1
													: 0;
		unsigned d = (design < 2) ? design : 0;
		return icon_table[d][mood];
	}

	/** Power the panel up for a refresh: rail on -> settle -> SPI active ->
	 * panel HW reset + re-init. The SSD1681 needs VCC stable before the reset,
	 * so wait out the regulator's startup (DT startup-delay-us is 10 ms; add a
	 * margin) before touching the bus. Order matters: SPI must be live before
	 * the panel RESUME, which talks over it. */
	void rail_resume()
	{
		regulator_enable(ext_3v3);
		k_msleep(15);
		pm_device_action_run(panel_spi, PM_DEVICE_ACTION_RESUME);
		pm_device_action_run(display, PM_DEVICE_ACTION_RESUME);
	}

	/** Reverse of rail_resume(): deep-sleep the panel (last command while the
	 * bus is still up), park the control lines low, float SCK/MOSI by suspending
	 * the SPI peripheral, then cut the rail. E-paper holds its image unpowered. */
	void rail_suspend()
	{
		pm_device_action_run(display, PM_DEVICE_ACTION_SUSPEND);
		gpio_pin_set_dt(&dc_pin, 0);  // DC active-high  -> physical low
		gpio_pin_set_dt(&rst_pin, 1); // RST active-low  -> physical low (held in reset)
		gpio_pin_set_dt(&cs_pin, 1);  // CS active-low   -> physical low
		pm_device_action_run(panel_spi, PM_DEVICE_ACTION_SUSPEND);
		regulator_disable(ext_3v3);
	}

	/** Driven by LVGL's render lifecycle: power+resume the panel just before LVGL
	 * pushes pixels, and power it back down right after the flush returns. The
	 * caller blocks inside the flush (the SSD16XX driver busy-waits on the BUSY
	 * line), so by RENDER_READY the refresh has finished and the rail is safe to
	 * cut. Between refreshes the rail is fully off (no panel/regulator draw). */
	void on_render_start(lv_event_t *)
	{
		rail_resume();
	}

	void on_render_ready(lv_event_t *)
	{
		rail_suspend();
	}

} // namespace

int ui_init()
{
	if (!device_is_ready(display))
	{
		LOG_ERR("Display not ready");
		return -ENODEV;
	}
	if (!device_is_ready(ext_3v3) || !device_is_ready(panel_spi))
	{
		LOG_ERR("Panel rail/SPI not ready");
		return -ENODEV;
	}

	lv_obj_t *scr = lv_scr_act();
	lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
	lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

	icon = lv_image_create(scr);
	lv_image_set_src(icon, &boot);  // Splash until the first reading lands.
	lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);

	pct_label = lv_label_create(scr);
	lv_obj_set_style_text_font(pct_label, &lv_font_montserrat_40, 0);
	lv_obj_set_style_text_color(pct_label, lv_color_black(), 0);
	lv_obj_set_style_bg_color(pct_label, lv_color_white(), 0);
	lv_obj_set_style_bg_opa(pct_label, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_hor(pct_label, 4, 0);
	lv_obj_set_style_pad_ver(pct_label, 2, 0);
	lv_label_set_text(pct_label, "");
	lv_obj_align(pct_label, LV_ALIGN_BOTTOM_MID, 0, -4);
	lv_obj_add_flag(pct_label, LV_OBJ_FLAG_HIDDEN);  // hidden until first reading

	display_blanking_off(display);

	/** Hook PM resume/suspend onto LVGL's render lifecycle so the panel
	 * is only powered for the duration of an actual refresh. */
	lv_display_t *disp = lv_display_get_default();
	lv_display_add_event_cb(disp, on_render_start, LV_EVENT_RENDER_START, nullptr);
	lv_display_add_event_cb(disp, on_render_ready, LV_EVENT_RENDER_READY, nullptr);

	/** Establish the steady-state: panel suspended, SPI parked, rail OFF. The
	 * boot-time controller init + display_blanking_off ran with the rail up
	 * (regulator-boot-on), leaving the device ACTIVE and the regulator refcount
	 * at 1. This one rail_suspend() balances it back to 0 (rail off) and marks
	 * the panel suspended, so the splash's RENDER_START actually re-inits it. */
	rail_suspend();

	/** LVGL runs on demand (no workqueue runner), so push the boot splash now.
	 * RENDER_START powers the rail back up for the refresh; RENDER_READY cuts it. */
	lv_refr_now(disp);
	return 0;
}

int ui_show_reading(int32_t /*mv*/, int percent)
{
	const lv_image_dsc_t *want_icon = icon_for_percent(percent);

	if (percent == last_shown_percent && want_icon == last_icon_src)
	{
		return 0;
	}

	char pct[16];
	snprintf(pct, sizeof(pct), "%d%%", percent);

	if (want_icon != last_icon_src)
	{
		lv_image_set_src(icon, want_icon);
		last_icon_src = want_icon;
	}
	lv_label_set_text(pct_label, pct);
	lv_obj_clear_flag(pct_label, LV_OBJ_FLAG_HIDDEN);  // reveal after first reading

	/** No workqueue runner: drive the flush here so the panel refreshes now and
	 * the RENDER_START/READY PM bracketing fires for this update. */
	lv_refr_now(lv_display_get_default());

	last_shown_percent = percent;
	return 0;
}
