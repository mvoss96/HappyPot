#pragma once
#include <stdint.h>

/** @file
 * The settings the user owns: kept across a reboot, and put where they have to act.
 *
 * The device's only persistent store, Zephyr settings over NVS in the "storage" partition -- the
 * same partition the Matter build keeps its fabrics in, under keys of our own ("happypot/...").
 * That sharing is the point: raw nvs_mount() and the settings subsystem cannot both own one
 * partition, and the Matter build brings settings whether we like it or not.
 *
 * One setter, not several: a setting has more than one home (flash, probe), and keeping them in
 * step used to be each caller's hand-written ritual. set() clamps, saves and applies; a new
 * setting is a row in prefs.cpp's table, enforced by static_assert.
 */
namespace prefs
{
	/** Every setting, in the table's order (prefs.cpp). */
	enum Id : uint8_t
	{
		CalDry, // probe mV in air   -> 0 %
		CalWet, // probe mV submerged -> 100 %
		COUNT,
	};

	/** Load what was saved -- but do not act on it yet (the probe is not up).
	 * A missing store is survivable: getters answer defaults, set() logs instead of saving.
	 * @retval 0 loaded; negative = store unavailable */
	int init();

	/** Put every setting where it acts (the probe's calibration). Once, after the probe is up. */
	void apply_all();

	/** Current value in stored units. */
	int32_t get(Id id);

	/** Valid range. The store clamps to it. */
	int32_t lo(Id id);
	int32_t hi(Id id);

	/** The user chose this on the panel: clamp, save, apply. Takes effect even if the flash
	 * write fails. A value that did not move costs nothing. */
	void set(Id id, int32_t v);
}
