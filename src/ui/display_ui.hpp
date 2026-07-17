#pragma once
#include <stdint.h>

/** @file
 * LVGL UI for the HappyPot 1.54" 200x200 e-paper. Exactly one content view at a time.
 *
 * The API is staging + commit: every set_*() only updates retained widgets (no panel I/O),
 * show_<view>() also selects the view, and one refresh() pushes ONE e-paper refresh for
 * everything staged -- a measurement cycle costs one refresh, not one per update. Setters dedup
 * at display resolution, because every avoided refresh is ~3 mAs.
 *
 * If init() fails (no display), everything below is a safe no-op.
 */
namespace ui
{
	/** Menu length limit. The panel decides this -- four 36 px rows fit above the hint line --
	 * and show_list() refuses anything longer rather than drawing rows over each other. */
	constexpr int LIST_MAX_ROWS = 4;

	/** What this build brought. Which menu rows exist is NOT the display's business -- it draws
	 * whatever list it is handed. */
	struct Config
	{
		/** Named on the boot splash, so a glance says which image is on the board. */
		const char *build = "Standalone";

		/** Onboarding payload ("MT:..."), rendered as a QR. NULL in a build with no radio (or
		 * with a radio that joins without one): then there is no QR draw buffer (the pool's
		 * largest allocation). The pairing view itself exists whenever `join_hint` or this is
		 * set -- see show_pairing(). */
		const char *pair_qr = nullptr;

		/** The same code for humans, drawn under the QR. Ignored if pair_qr is NULL. */
		const char *pair_manual = nullptr;
	};

	/** Build all widgets (resident, once) and show the boot splash.
	 * @retval -1 no display; every other function becomes a no-op */
	int init(const Config &cfg = {});

	/** The pairing view: the QR while there is something to scan, "CONNECTED" once there is
	 * not. No-op when init() was given no codes. */
	void show_pairing(bool commissioned);

	/** Show the sensor view with whatever reading it holds. The one view the device returns
	 * to -- also how a menu or an error is dismissed. Selecting is separate from filling: a
	 * reading that arrives while another view is up must not yank the screen. */
	void show_sensor();

	/** Stage the reading: the mood face follows the percent, the label shows it. Deduped at
	 * displayed resolution, so a change too small to show costs no refresh. */
	void set_sensor(int32_t mv, int percent);

	/** The low-battery view (a resting view: up until the battery is not). */
	void set_low_battery(int percent);

	/** The error view: a headline and a detail line, both required. */
	void set_error(const char *title, const char *detail);

	/** The factory-reset confirmation. A reset drops every fabric and cannot be undone, so the
	 * destructive answer is the deliberate gesture (hold), not the reflex (tap). */
	void set_reset_prompt();

	/** The menu view: `count` rows, cursor on `selected` (drawn in reverse video). ONE menu
	 * view draws every list it is handed -- whose rows they are is menu.cpp's business. Lists
	 * longer than LIST_MAX_ROWS are refused. */
	void show_list(const char *const *labels, int count, int selected);

	/** The calibration prompt: which endpoint is about to be sampled, and what the probe must
	 * be in when it is. Sampling in the wrong medium miscalibrates the scale; this screen is
	 * the whole safety net. */
	void set_calib_prompt(bool wet);

	/** The calibration-defaults confirmation. */
	void set_calib_reset_prompt();

	/** The captured endpoint, on its way into the store. */
	void set_calib_result(int32_t mv);

	/** Commit every staged change with a single e-paper refresh: full on a view change and
	 * periodically (ghosting), partial otherwise. Does nothing if nothing changed. */
	void refresh();

	/** Log the LVGL pool's headline numbers (target build only). */
	void log_pool(const char *tag);
}
