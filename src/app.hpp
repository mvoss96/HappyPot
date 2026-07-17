#pragma once

/** @file
 * The device: display, soil probe, battery, button and the loop tying them together.
 *
 * A module, not a main(): the BTHome firmware runs it as its only thread, the Matter build on a
 * thread of its own next to CHIP/OpenThread (apps/matter/src/app_task.cpp). Everything the loop
 * says to the world beyond the panel goes through net (net.hpp); a build that installs no hooks
 * there has no radio, and the loop never asks twice.
 *
 * The loop owns the panel: LVGL is not thread-safe, so nothing outside run()'s thread may touch
 * the UI. Other threads leave values where the loop picks them up (see net::set_link_up()).
 */
namespace app
{
	/** Bring up display, soil probe, battery and button, then run the device.
	 * @param build_name names this build on the splash and in the boot log -- the firmwares
	 *                   look alike everywhere else. Not copied. */
	void run(const char *build_name = "Standalone");
}
