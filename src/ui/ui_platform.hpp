#pragma once

/** @file
 * Thin platform seam for the LVGL UI.
 *
 * On the nRF target these forward to the Zephyr display driver (ui_platform.cpp);
 * in the host preview build the sim provides its own no-op implementation
 * (sim/ui_platform_sim.cpp). This lets the exact same display_ui.cpp render both
 * on the panel and on the PC.
 */
namespace plat
{

	/** Is the panel usable?
	 *
	 * @return true if the display device is ready; false on the target when it
	 *         failed to initialise (always true in the host sim)
	 */
	bool display_ready();

	/** Select the full-refresh profile for the next flush. */
	void blanking_on();

	/** Leave blanking, which is what triggers the full panel update. */
	void blanking_off();

	/** Write a diagnostic line.
	 *
	 * @param msg NUL-terminated text, newline included by the caller
	 */
	void log(const char *msg);

	/** Wake the panel for a refresh.
	 * Pair with display_suspend() around the WHOLE refresh -- including
	 * blanking_off(), which is where the ssd16xx actually triggers the full-refresh
	 * panel update. Suspending earlier (e.g. on LVGL's RENDER_READY) would sleep the
	 * panel before that update and leave it blank. On nRF this runs the ssd16xx
	 * pm_device RESUME action (wake + reset + profile reload); the pm subsystem
	 * tracks state, so the redundant RESUME on the already-active boot render is a
	 * no-op. In the host sim it does nothing.
	 */
	void display_resume();

	/** Put the panel into deep sleep mode 1 until the next refresh. */
	void display_suspend();

} // namespace plat
