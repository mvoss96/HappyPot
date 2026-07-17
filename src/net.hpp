#pragma once

#include "sensors/battery.hpp"
#include "sensors/soil.hpp"

/** @file
 * The device's edge to a network, whichever network that is.
 *
 * app.cpp measures, waits and paints; everything it says to the outside world goes through here.
 * A network build installs the hooks below (apps/bthome/src/bthome_adv.cpp, or
 * apps/matter/src/app_task.cpp); a build that installs nothing gets every function quietly
 * answering "no" -- which is why the loop has no #ifdefs.
 *
 * The API is deliberately radio-agnostic: a Radio descriptor instead of Matter pairing codes,
 * set_link_up() instead of "thread connected". A future Zigbee build must fit this seam without
 * a single change to src/ -- that is the test the seam has to pass.
 *
 * Threading: hooks are called on the loop's thread (a hook that blocks stalls the panel). The
 * set_* functions marked below are called from the network's threads; they only store a word,
 * which is atomic on this core, and the loop reads it on its next pass.
 */
namespace net
{
	/** One cycle's worth of truth; every radio gets the whole thing (BTHome packs all four
	 * fields into a single advert; Matter fans them out to clusters). */
	struct Reading
	{
		SoilReading soil;
		BatteryReading battery;
	};

	/** What a network build provides. Any pointer may be null: a null hook is a feature this
	 * build does not have. */
	struct Hooks
	{
		/** The reading, every cycle. Forwarded unconditionally -- BTHome is a beacon protocol
		 * and a stopped beacon reads as a dead device; Matter no-ops an unchanged attribute
		 * write. A transport that charges for repeats dedups in ITS hook, not here. */
		void (*reading)(const Reading &r);

		/** Drop every network the device is on; the user confirmed it on the panel. */
		void (*factory_reset)();

		/** The onboarding code is on the panel: start listening. Idempotent. */
		void (*pairing_open)();
	};

	/** Who the radio is -- not which #ifdef built it. Strings are not copied; hand over
	 * literals or buffers that outlive the loop. */
	struct Radio
	{
		const char *name;		 /**< "BTHome", "Matter over Thread", "Zigbee". */
		const char *pair_qr;	 /**< Onboarding payload ("MT:...") or null (BTHome: null). */
		const char *pair_manual; /**< The same code for humans, or null. */
		const char *join_hint;	 /**< Joining hint when there is no QR, or null. */
	};

	/** Install the network. Call before app::run(). */
	void set_hooks(const Hooks &hooks);

	/** Install the radio's identity. Its presence is what makes the device HAVE a radio --
	 * menu row, pairing view and splash all key on it (has_radio()), never on a compile
	 * switch. Call before app::run(). */
	void set_radio(const Radio &radio);

	/** The installed radio, or null without one. */
	const Radio *radio();

	/** Whether there is a network to speak of. The menu asks before it offers a row. */
	bool has_radio();
	bool can_factory_reset();

	/** Whether the device is on a network (Matter: on a fabric; Zigbee: on a PAN). Written
	 * from the network's threads -- seeded at boot, then kept current by its delegate. */
	void set_commissioned(bool on_network);
	bool commissioned();

	/** Whether the link is up (Thread attached, Zigbee joined). Written from the network's
	 * threads; the loop reads it on its next pass. */
	void set_link_up(bool up);
	bool link_up();

	/** Forwarders: each calls its hook if there is one, so callers never test for the
	 * network themselves. */
	void publish_reading(const Reading &r);
	void open_pairing();
	void factory_reset();
}
