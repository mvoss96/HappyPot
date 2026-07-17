#pragma once
#include <stdint.h>

#include "input/button.hpp"

/** @file
 * The settings menu. Exists only while the user is inside it: knows nothing about readings or
 * the cadence, never refreshes the panel, only stages its own views. The loop (app.cpp) owns
 * the rest.
 *
 * WHAT the menu contains is a table at the top of menu.cpp -- a row says what KIND it is
 * (sub-menu, screen, way out) and the machinery knows how each kind behaves. Adding an entry is
 * adding a row.
 *
 * The shape: tap = next row, hold = activate, 30 s idle = out. A Screen runs its own little
 * flow and ends up back in the list -- except the pairing view (scanned or dismissed) and a
 * confirmed factory reset, which leave the menu via Status below.
 */
namespace menu
{
	/** What proceed() wants the loop to do next. */
	enum class Status : uint8_t
	{
		Running,	 // still inside
		Exited,		 // show the readings again
		FactoryReset // confirmed; the loop hands it to net::factory_reset()
	};

	/** Open the menu on its first entry. */
	void enter();

	/** Open the menu directly on the pairing screen -- the boot onboarding of a device that has
	 * never been commissioned. From there it behaves like any menu: the loop drives it with
	 * proceed(), and it exits (scanned, or dismissed) to the readings. */
	void enter_network();

	/** Advance the menu: one gesture, plus whatever its own clock says. Safe with Event::None --
	 * that is how result countdowns and idle timeouts get their turn. */
	Status proceed(button::Event e);

	/** When proceed() must run again unprompted (result screen end, idle timeout).
	 * @return absolute k_uptime_get() deadline */
	int64_t deadline_ms();

	/** Leave immediately. For the low-battery warning. */
	void abort();
}
