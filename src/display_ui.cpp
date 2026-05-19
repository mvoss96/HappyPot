#include "display_ui.h"

#include <stdio.h>
#include <lvgl.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>

#include "app_config.h"
#include "ui_images/ui_images.h"

LOG_MODULE_REGISTER(ui, LOG_LEVEL_INF);

namespace
{
	const device *const display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

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

	/** Driven by the LVGL workqueue thread: resume the panel just before LVGL
	 * starts pushing pixels, and put it back to deep sleep right after the flush
	 * returns. The workqueue thread blocks inside the flush (the SSD16XX driver
	 * busy-waits on the BUSY line), so by the time RENDER_READY fires the panel
	 * has finished its refresh and is safe to sleep. */
	void on_render_start(lv_event_t *)
	{
		pm_device_action_run(display, PM_DEVICE_ACTION_RESUME);
	}

	void on_render_ready(lv_event_t *)
	{
		pm_device_action_run(display, PM_DEVICE_ACTION_SUSPEND);
	}

} // namespace

int ui_init()
{
	if (!device_is_ready(display))
	{
		LOG_ERR("Display not ready");
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

	last_shown_percent = percent;
	return 0;
}
