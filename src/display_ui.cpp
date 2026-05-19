#include "display_ui.h"

#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/display/cfb.h>
#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>

#include "app_config.h"

LOG_MODULE_REGISTER(ui, LOG_LEVEL_INF);

namespace {

const device *const display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

constexpr int k_panel_w = 200;
constexpr int k_panel_h = 200;

/** CFB font indices in link order. Check the init log (or cfb shell command)
 * to confirm which registered font lives at which index, then update here. */
constexpr uint8_t k_big_font_idx = 2;
constexpr uint8_t k_small_font_idx = 0;

struct FontInfo { uint8_t idx, w, h; };

FontInfo big_font{k_big_font_idx, 0, 0};
FontInfo small_font{k_small_font_idx, 0, 0};

int last_shown_percent = 0;
bool has_shown = false;

void display_pm(pm_device_action action, const char *what)
{
	int ret = pm_device_action_run(display, action);
	if (ret < 0 && ret != -EALREADY) {
		LOG_WRN("display %s failed (%d)", what, ret);
	}
}

int centered_x(const char *s, uint8_t font_w)
{
	int x = (k_panel_w - static_cast<int>(strlen(s)) * font_w) / 2;
	return x < 0 ? 0 : x;
}

}  // namespace

int ui_init()
{
	if (!device_is_ready(display)) {
		LOG_ERR("Display not ready");
		return -ENODEV;
	}

	if (cfb_framebuffer_init(display)) {
		LOG_ERR("CFB init failed");
		return -EIO;
	}

	display_blanking_off(display);
	cfb_framebuffer_invert(display);

	cfb_get_font_size(display, big_font.idx, &big_font.w, &big_font.h);
	cfb_get_font_size(display, small_font.idx, &small_font.w, &small_font.h);

	LOG_INF("big font idx=%d %dx%d, small idx=%d %dx%d",
		big_font.idx, big_font.w, big_font.h,
		small_font.idx, small_font.w, small_font.h);
	return 0;
}

int ui_show_reading(int32_t mv, int percent)
{
	int delta = percent - last_shown_percent;
	if (delta < 0) delta = -delta;
	if (has_shown && delta < cfg::DISPLAY_PCT_THRESHOLD) {
		return 0;
	}

	display_pm(PM_DEVICE_ACTION_RESUME, "resume");

	char pct[16];
	char raw[16];
	snprintf(pct, sizeof(pct), "%d%%", percent);
	snprintf(raw, sizeof(raw), "%d mV", static_cast<int>(mv));

	cfb_framebuffer_clear(display, false);

	/** Big percent, centered. */
	cfb_framebuffer_set_font(display, big_font.idx);
	int py = (k_panel_h - big_font.h) / 2 - 10;
	if (py < 0) py = 0;
	cfb_print(display, pct, centered_x(pct, big_font.w), py);

	/** Small mV line below, centered. */
	cfb_framebuffer_set_font(display, small_font.idx);
	cfb_print(display, raw, centered_x(raw, small_font.w), py + big_font.h + 8);

	const int ret = cfb_framebuffer_finalize(display);

	display_pm(PM_DEVICE_ACTION_SUSPEND, "suspend");

	if (ret == 0) {
		last_shown_percent = percent;
		has_shown = true;
	}
	return ret;
}
