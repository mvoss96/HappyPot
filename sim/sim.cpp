/** @file
 * Host preview harness for the HappyPot LVGL UI (see build.ps1).
 *
 * Compiles the real UI (src/ui/display_ui.cpp) against LVGL on the PC, renders
 * each screen into an L8 buffer via a headless flush callback, thresholds it to pure
 * black/white (to emulate the 1-bit e-paper) and writes one PNG per screen.
 *
 * The PNG encoder is deliberately kept in its own png_writer.h so it can be
 * swapped/dropped independently (see the note there).
 */
#include <lvgl.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "ui/display_ui.hpp"
#include "png_writer.h"
#include "bmp_writer.h"

// The platform seam (plat::) is implemented in ui_platform_sim.cpp.

// ---- headless LVGL display + snapshot ----
namespace
{

	constexpr int W = 200, H = 200; // matches the SSD1681 canvas in display_ui.cpp
	uint8_t g_fb[W * H];			// captured L8 frame (0=black..255=white)
	uint32_t g_tick_ms = 0;

	/** LVGL's clock source. @return the fake time in milliseconds */
	uint32_t tick_cb() { return g_tick_ms; }

	/** Headless flush: copy LVGL's rendered area into g_fb instead of a panel.
	 *
	 * @param disp   the display being flushed; must be told when we are done
	 * @param area   the rectangle LVGL rendered, in screen coordinates
	 * @param px_map its pixels, one L8 byte each, row-major within the area
	 */
	void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
	{
		const int aw = area->x2 - area->x1 + 1;
		for (int y = area->y1; y <= area->y2; y++)
			for (int x = area->x1; x <= area->x2; x++)
				g_fb[y * W + x] = px_map[(y - area->y1) * aw + (x - area->x1)];
		lv_display_flush_ready(disp);
	}

	/** Threshold the captured frame to pure black/white and write it out.
	 *
	 * @param name basename of the pair to write: <name>.png and <name>.bmp
	 */
	void snapshot(const char *name)
	{
		static uint8_t bw[W * H];
		for (int i = 0; i < W * H; i++)
			bw[i] = (g_fb[i] >= 128) ? 255 : 0;

		char path[64];
		std::snprintf(path, sizeof(path), "%s.png", name);
		int rc = write_gray_png(path, bw, W, H);
		std::snprintf(path, sizeof(path), "%s.bmp", name);
		rc |= write_gray_bmp(path, bw, W, H);

		std::printf(rc == 0 ? "wrote %s.{png,bmp}\n" : "FAILED %s\n", name);
	}

	/** Stage + commit + snapshot one screen, advancing the fake clock first. */
	void shoot(const char *name)
	{
		g_tick_ms += 100;
		ui::refresh();
		snapshot(name);
	}

} // namespace

/** Render every screen of one build and write the PNG/BMP pairs.
 *
 * The two builds do not draw the same device: the Matter one names itself on the splash, has a
 * Network menu row and a pairing screen behind it. So the sim renders whichever it was asked
 * for -- and build.ps1 asks for both, because a screen that only one build has is exactly the
 * screen a mockup is needed for.
 *
 * @param argc 2
 * @param argv [1] = "bthome" or "matter"
 *
 * @retval 0 all screens rendered
 * @retval 1 bad arguments, or ui::init() failed
 */
int main(int argc, char **argv)
{
	if (argc != 2)
	{
		std::printf("usage: happypot_sim <bthome|matter>\n");
		return 1;
	}
	const bool matter = std::strcmp(argv[1], "matter") == 0;
	if (!matter && std::strcmp(argv[1], "bthome") != 0)
	{
		std::printf("unknown build '%s'\n", argv[1]);
		return 1;
	}

	lv_init();
	lv_tick_set_cb(tick_cb);

	static uint8_t buf[W * H];
	lv_display_t *disp = lv_display_create(W, H);
	lv_display_set_color_format(disp, LV_COLOR_FORMAT_L8);
	lv_display_set_buffers(disp, buf, nullptr, sizeof(buf), LV_DISPLAY_RENDER_MODE_FULL);
	lv_display_set_flush_cb(disp, flush_cb);

	/* Exactly what each build hands ui::init() on the device (app::run). The Matter payload is
	 * the well-known test-passcode one, so the QR in the mockup scans like the panel's. The
	 * BTHome build hands over no codes -- and that absence is what takes the QR's draw buffer
	 * out of its LVGL pool and the pairing view out of its build. */
	const ui::Config cfg = matter ? ui::Config{
										.build = "Matter over Thread",
										.pair_qr = "MT:Y.K9042C00KA0648G00",
										.pair_manual = "3497-011-2332",
									}
								  : ui::Config{.build = "BTHome"};
	if (ui::init(cfg) != 0)
	{
		std::printf("ui::init failed\n");
		return 1;
	}
	snapshot("boot");

	// show_sensor() selects the view, set_sensor() fills it -- as the loop's measure() does.
	// One snapshot per mood, because the face IS the reading.
	ui::show_sensor();
	ui::set_sensor(1520, 78); // happy
	shoot("sensor_happy");

	ui::set_sensor(1710, 45); // meh
	shoot("sensor_meh");

	ui::set_sensor(1930, 12); // thirsty
	shoot("sensor_thirsty");

	ui::set_low_battery();
	shoot("lowbat");

	// The menu's corner readout, as the loop stages it after every measurement.
	ui::set_battery(87);

	// The menu. It is a list of strings with a cursor -- the display does not know what the
	// entries mean, so the sim says them out loud, exactly as menu.cpp's tables would.
	const char *root_matter[] = {"Calibrate", "Network", "Factory reset", "Exit"};
	const char *root_plain[] = {"Calibrate", "Exit"};
	const char *const *root = matter ? root_matter : root_plain;
	const int root_n = matter ? 4 : 2;

	for (int i = 0; i < root_n; i++)
	{
		g_tick_ms += 100;
		ui::show_list(root, root_n, i);
		ui::refresh();
		char nm[24];
		std::snprintf(nm, sizeof(nm), "menu_%d", i);
		snapshot(nm);
	}

	// The Calibrate sub-menu: the same list widget, a second time, for nothing.
	const char *cal[] = {"Wet point", "Dry point", "Defaults", "Back"};
	for (int i = 0; i < 4; i++)
	{
		g_tick_ms += 100;
		ui::show_list(cal, 4, i);
		ui::refresh();
		char nm[24];
		std::snprintf(nm, sizeof(nm), "calmenu_%d", i);
		snapshot(nm);
	}

	// The calibration flow, one snapshot per step the user walks through.
	ui::set_calib_prompt(true);
	shoot("cal_prompt_wet");

	ui::set_calib_prompt(false);
	shoot("cal_prompt_dry");

	ui::set_calib_reset_prompt();
	shoot("cal_reset_prompt");

	ui::set_calib_result();
	shoot("cal_result");

	if (matter)
	{
		// The pairing screen, both halves. Uncommissioned: something to scan.
		// Commissioned: nothing to scan, so the state instead.
		ui::show_pairing(/*commissioned=*/false);
		shoot("pairing");

		ui::show_pairing(/*commissioned=*/true);
		shoot("pairing_connected");

		// Reached by holding on the Factory reset row. Tap cancels; hold drops every fabric.
		ui::set_reset_prompt();
		shoot("reset_prompt");
	}

	ui::set_error("PROBE ERROR", "soil read failed");
	shoot("error");

	std::printf("done\n");
	return 0;
}
