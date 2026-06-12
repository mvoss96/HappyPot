#include "display_ui.h"

#include <stdio.h>
#include <string.h>
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
	lv_obj_t *boot_meta_label;
	lv_obj_t *low_batt_icon;
	lv_obj_t *calib_icon;
	lv_obj_t *calib_label;

	unsigned design = 1;
	int last_shown_percent = -1;
	int last_battery_percent = -1;
	const lv_image_dsc_t *last_icon_src = nullptr;
	bool low_batt_visible = false;

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

	const lv_image_dsc_t *calib_image_for(const char *label)
	{
		if (strcmp(label, "WET") == 0)    return &cal_wet;
		if (strcmp(label, "DRY") == 0)    return &cal_dry;
		if (strcmp(label, "RESET") == 0)  return &cal_reset;
		if (strcmp(label, "CANCEL") == 0) return &cal_reset;
		return &cal_done;
	}

	/** Bracket each refresh with the panel's own power management. RENDER_START
	 * wakes the SSD1681 (HW reset + minimal re-init); RENDER_READY puts it into
	 * Deep Sleep Mode 1, which drops the controller to ~uA *while retaining its
	 * RAM* (mode 1, not mode 2). RAM retention is what lets the next refresh be a
	 * partial update. VCC stays up the whole time (panel rail is wired directly to
	 * BAT/VCC on this hardware),
	 * so only the controller deep-sleeps, not the rail. The caller blocks inside
	 * the flush (the driver busy-waits on BUSY), so by RENDER_READY the refresh
	 * has finished and deep sleep is safe. */
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

	boot_meta_label = lv_label_create(scr);
	lv_obj_set_style_text_font(boot_meta_label, &lv_font_montserrat_16, 0);
	lv_obj_set_style_text_color(boot_meta_label, lv_color_black(), 0);
	lv_obj_set_style_text_opa(boot_meta_label, LV_OPA_70, 0);
	lv_obj_set_style_text_align(boot_meta_label, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_style_bg_color(boot_meta_label, lv_color_white(), 0);
	lv_obj_set_style_bg_opa(boot_meta_label, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_hor(boot_meta_label, 6, 0);
	lv_obj_set_style_pad_ver(boot_meta_label, 2, 0);
	lv_obj_set_width(boot_meta_label, lv_pct(100));
	char boot_meta[64];
	snprintf(boot_meta,
			 sizeof(boot_meta),
			 "v%u.%u.%u",
			 cfg::FW_VERSION_MAJOR,
			 cfg::FW_VERSION_MINOR,
			 cfg::FW_VERSION_PATCH);
	lv_label_set_text(boot_meta_label, boot_meta);
	lv_obj_align(boot_meta_label, LV_ALIGN_BOTTOM_MID, 0, -2);

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

	low_batt_icon = lv_image_create(scr);
	lv_image_set_src(low_batt_icon, &lowbat);
	lv_obj_align(low_batt_icon, LV_ALIGN_CENTER, 0, 0);
	lv_obj_add_flag(low_batt_icon, LV_OBJ_FLAG_HIDDEN);

	calib_icon = lv_image_create(scr);
	lv_obj_align(calib_icon, LV_ALIGN_CENTER, 0, 0);
	lv_obj_add_flag(calib_icon, LV_OBJ_FLAG_HIDDEN);

	calib_label = lv_label_create(scr);
	lv_obj_set_style_text_font(calib_label, &lv_font_montserrat_40, 0);
	lv_obj_set_style_text_color(calib_label, lv_color_black(), 0);
	lv_obj_set_style_bg_color(calib_label, lv_color_white(), 0);
	lv_obj_set_style_bg_opa(calib_label, LV_OPA_COVER, 0);
	lv_obj_set_style_text_align(calib_label, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(calib_label, lv_pct(100));
	lv_obj_align(calib_label, LV_ALIGN_BOTTOM_MID, 0, -4);
	lv_obj_add_flag(calib_label, LV_OBJ_FLAG_HIDDEN);

	display_blanking_off(display);

	/** Hook the panel's deep-sleep onto LVGL's render lifecycle so it is only
	 * awake for the duration of an actual refresh. */
	lv_display_t *disp = lv_display_get_default();
	lv_display_add_event_cb(disp, on_render_start, LV_EVENT_RENDER_START, nullptr);
	lv_display_add_event_cb(disp, on_render_ready, LV_EVENT_RENDER_READY, nullptr);

	/** LVGL runs on demand (no workqueue runner), so push the boot splash now. */
	lv_refr_now(disp);
	return 0;
}

int ui_show_reading(int32_t /*mv*/, int percent)
{
	const lv_image_dsc_t *want_icon = icon_for_percent(percent);

	if (!low_batt_visible && percent == last_shown_percent && want_icon == last_icon_src)
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
	lv_obj_clear_flag(icon, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(boot_meta_label, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(low_batt_icon, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(calib_icon, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(calib_label, LV_OBJ_FLAG_HIDDEN);
	lv_label_set_text(pct_label, pct);
	lv_obj_clear_flag(pct_label, LV_OBJ_FLAG_HIDDEN);  // reveal after first reading

	/** No workqueue runner: drive the refresh here. RENDER_START/READY wake the
	 * panel and put it back to deep sleep around this flush. */
	lv_refr_now(lv_display_get_default());

	low_batt_visible = false;
	last_shown_percent = percent;
	return 0;
}

int ui_show_low_battery(int battery_percent)
{
	if (battery_percent < 0)
	{
		battery_percent = 0;
	}
	if (battery_percent > 100)
	{
		battery_percent = 100;
	}

	if (low_batt_visible && battery_percent == last_battery_percent)
	{
		return 0;
	}

	lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(pct_label, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(boot_meta_label, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(calib_icon, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(calib_label, LV_OBJ_FLAG_HIDDEN);
	lv_obj_clear_flag(low_batt_icon, LV_OBJ_FLAG_HIDDEN);

	lv_refr_now(lv_display_get_default());

	low_batt_visible = true;
	last_battery_percent = battery_percent;
	return 0;
}

static void calib_show_common(const lv_image_dsc_t *img)
{
	lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(pct_label, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(boot_meta_label, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(low_batt_icon, LV_OBJ_FLAG_HIDDEN);
	lv_image_set_src(calib_icon, img);
	lv_obj_clear_flag(calib_icon, LV_OBJ_FLAG_HIDDEN);
	last_shown_percent = -1;
	low_batt_visible = false;
}

void ui_show_calibration_step(const char *label, int32_t mv)
{
	calib_show_common(&cal_done);
	char buf[16];
	snprintf(buf, sizeof(buf), "%ld mV", (long)mv);
	lv_label_set_text(calib_label, buf);
	lv_obj_clear_flag(calib_label, LV_OBJ_FLAG_HIDDEN);

	lv_refr_now(lv_display_get_default());
}

void ui_show_calibration_step(const char *label)
{
	calib_show_common(calib_image_for(label));
	lv_obj_add_flag(calib_label, LV_OBJ_FLAG_HIDDEN);

	lv_refr_now(lv_display_get_default());
}

void ui_exit_calibration()
{
	lv_obj_add_flag(calib_icon, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(calib_label, LV_OBJ_FLAG_HIDDEN);
	lv_obj_clear_flag(icon, LV_OBJ_FLAG_HIDDEN);
	lv_obj_clear_flag(pct_label, LV_OBJ_FLAG_HIDDEN);
	last_shown_percent = -1;
	low_batt_visible = false;

	lv_refr_now(lv_display_get_default());
}
