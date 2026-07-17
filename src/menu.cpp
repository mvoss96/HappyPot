#include "menu.hpp"

#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app_config.hpp"
#include "net.hpp"
#include "prefs.hpp"
#include "sensors/soil.hpp"
#include "ui/display_ui.hpp"

LOG_MODULE_REGISTER(menu, LOG_LEVEL_INF);

/* The menu, as data: everything a person would change -- a new entry, a different order -- is IN
 * the tables below; everything under them is machinery that knows each row KIND (sub-menu,
 * screen, way out) exactly once. */

namespace
{
	constexpr int64_t MENU_IDLE_MS = 30000;	   // an untouched menu returns to the readings
	constexpr int64_t PROMPT_IDLE_MS = 120000; // long enough to fetch a glass of water
	constexpr int64_t RESULT_MS = 3000;		   // the captured mV stays up this long

	// ---- what a row can be ------------------------------------------------------------------

	/* The menus, by name. They are menu.cpp's and nobody else's: the panel has one menu view and
	 * draws whatever list of strings it is handed. */
	enum class List : uint8_t
	{
		Root,
		Calibrate,
		Count,
	};

	enum class Kind : uint8_t
	{
		Submenu, // opens another list
		Screen,	 // opens a view with a flow of its own
		Leave,	 // out of this list: back to its parent, or out of the menu altogether
	};

	enum class Screen : uint8_t
	{
		CalWet,
		CalDry,
		CalReset,
		Network,
		FactoryReset,
	};

	struct Row
	{
		Kind kind;
		const char *label;
		uint8_t id = 0;				 // a List or a Screen -- whichever `kind` says
		bool (*present)() = nullptr; // nullptr = always; else the row exists only where true
	};

	// ---- THE MENU ---------------------------------------------------------------------------

	const Row ROOT[] = {
		{Kind::Submenu, "Calibrate", (uint8_t)List::Calibrate},
		{Kind::Screen, "Network", (uint8_t)Screen::Network, net::has_radio},
		{Kind::Screen, "Factory reset", (uint8_t)Screen::FactoryReset, net::can_factory_reset},
		{Kind::Leave, "Exit"},
	};

	const Row CALIBRATE[] = {
		{Kind::Screen, "Wet point", (uint8_t)Screen::CalWet},
		{Kind::Screen, "Dry point", (uint8_t)Screen::CalDry},
		{Kind::Screen, "Defaults", (uint8_t)Screen::CalReset},
		{Kind::Leave, "Back"},
	};

	/* Both lists are AT the panel's limit, and the panel draws fixed arrays of that size -- a
	 * fifth row would write past them. A list that needs five entries needs a sub-menu. */
	static_assert(sizeof(ROOT) / sizeof(ROOT[0]) <= ui::LIST_MAX_ROWS, "the root menu is too long");
	static_assert(sizeof(CALIBRATE) / sizeof(CALIBRATE[0]) <= ui::LIST_MAX_ROWS,
				  "the Calibrate menu is too long");

	struct ListDef
	{
		const Row *rows;
		int count;
		List parent; // where a Leave row goes; Root leaves the menu entirely
	};

	const ListDef LISTS[] = {
		/* Root      */ {ROOT, (int)(sizeof(ROOT) / sizeof(ROOT[0])), List::Root},
		/* Calibrate */ {CALIBRATE, (int)(sizeof(CALIBRATE) / sizeof(CALIBRATE[0])), List::Root},
	};
	static_assert(sizeof(LISTS) / sizeof(LISTS[0]) == (size_t)List::Count,
				  "a list with no definition");

	// ---- state ------------------------------------------------------------------------------

	/* Only the things that have a FLOW get a state. */
	enum class State : uint8_t
	{
		List, // a menu is on screen; `list` says which
		CalPrompt,
		CalResult,
		CalResetPrompt,
		Pairing,
		ResetPrompt,
	};

	State state = State::List;
	List list = List::Root;
	int cursor[(int)List::Count]; // where the user left each list

	bool cal_wet;	 // which endpoint CalPrompt/CalResult are about
	bool pairing_qr; // which half of the Network screen is up: the QR, or CONNECTED

	int64_t idle_at; // every state that can be walked away from times out here

	const char *name(State s)
	{
		switch (s)
		{
		case State::CalPrompt: return "cal-prompt";
		case State::CalResult: return "cal-result";
		case State::CalResetPrompt: return "cal-reset";
		case State::Pairing: return "pairing";
		case State::ResetPrompt: return "reset-prompt";
		default: return "list";
		}
	}

	/** The only place `state` is assigned, so no transition can go unlogged. */
	void go(State next)
	{
		if (next != state)
		{
			LOG_INF("[UI] menu %s -> %s", name(state), name(next));
		}
		state = next;
	}

	// ---- drawing a list ---------------------------------------------------------------------

	/** Whether a row exists on this device. A build with no radio has nothing to pair over, and
	 * nothing to reset -- so those rows are not drawn, and the cursor must not stop on them
	 * either, or a tap would appear to do nothing. */
	bool visible(const Row &r) { return r.present == nullptr || r.present(); }

	/** Draw the current list: visible rows, cursor on `sel` (counted over VISIBLE rows). */
	void draw(int sel)
	{
		const ListDef &def = LISTS[(int)list];

		const char *labels[ui::LIST_MAX_ROWS];
		int n = 0;

		for (int i = 0; i < def.count; i++)
		{
			if (visible(def.rows[i]))
			{
				labels[n++] = def.rows[i].label;
			}
		}

		ui::show_list(labels, n, sel);
	}

	/** The row the cursor is on, skipping the ones this build does not have. */
	const Row &row_at(int sel)
	{
		const ListDef &def = LISTS[(int)list];
		int n = 0;
		for (int i = 0; i < def.count; i++)
		{
			if (visible(def.rows[i]) && n++ == sel)
			{
				return def.rows[i];
			}
		}
		return def.rows[def.count - 1]; // unreachable: sel is always a visible row
	}

	int visible_count()
	{
		const ListDef &def = LISTS[(int)list];
		int n = 0;
		for (int i = 0; i < def.count; i++)
		{
			n += visible(def.rows[i]) ? 1 : 0;
		}
		return n;
	}

	/** Show a list, with the cursor where the user left it. */
	void to_list(List l)
	{
		list = l;
		go(State::List);
		idle_at = k_uptime_get() + MENU_IDLE_MS;
		draw(cursor[(int)l]);
	}

	// ---- the screens, each with its own flow -------------------------------------------------

	/** The Network screen, two halves by net::commissioned().
	 *
	 * The QR half: opening it also opens the commissioning window (the moment the code is on the
	 * panel is the moment somebody means to scan it), and it has NO idle timeout -- an
	 * uncommissioned device's one job is this code, and e-paper keeps it up for free. idle_at is
	 * only the poll that lets proceed() notice a commissioner without a gesture.
	 *
	 * The CONNECTED half: a status page, read and left, on the usual prompt timeout. */
	void to_pairing()
	{
		go(State::Pairing);
		pairing_qr = !net::commissioned();
		if (pairing_qr)
		{
			net::open_pairing();
			idle_at = k_uptime_get() + MENU_IDLE_MS; // the poll, not a timeout
		}
		else
		{
			idle_at = k_uptime_get() + PROMPT_IDLE_MS;
		}
		ui::show_pairing(!pairing_qr);
	}

	/** Ask before sampling an endpoint: the probe has to be IN the right medium first, and a
	 * capture in the wrong one miscalibrates the scale. Long timeout -- fetching a glass of
	 * water is part of the flow. */
	void to_cal_prompt(bool wet)
	{
		cal_wet = wet;
		go(State::CalPrompt);
		idle_at = k_uptime_get() + PROMPT_IDLE_MS;
		ui::set_calib_prompt(wet);
	}

	/** Capture the endpoint and show it. prefs clamps, saves and pushes the pair into the
	 * probe -- and refuses a dry==wet pair on the apply side. */
	void to_cal_result()
	{
		int32_t mv;
		if (soil::sample_raw(&mv) != 0)
		{
			LOG_WRN("[CAL] read failed");
			to_list(list);
			return;
		}

		prefs::set(cal_wet ? prefs::CalWet : prefs::CalDry, mv);
		LOG_INF("[CAL] saved %s: %d mV (dry=%d wet=%d)", cal_wet ? "wet" : "dry", mv,
				soil::dry_mv(), soil::wet_mv());

		go(State::CalResult);
		idle_at = k_uptime_get() + RESULT_MS;
		ui::set_calib_result(mv);
	}

	void to_cal_reset_prompt()
	{
		go(State::CalResetPrompt);
		idle_at = k_uptime_get() + MENU_IDLE_MS;
		ui::set_calib_reset_prompt();
	}

	/** Ask before dropping every network. One button, an answer that cannot be taken back: tap
	 * is the reflex, so tap is the harmless one. */
	void to_reset_prompt()
	{
		go(State::ResetPrompt);
		idle_at = k_uptime_get() + MENU_IDLE_MS;
		ui::set_reset_prompt();
	}

	// ---- what a hold on a row does ------------------------------------------------------------

	menu::Status activate(int sel)
	{
		const Row &r = row_at(sel);

		switch (r.kind)
		{
		case Kind::Submenu:
			to_list((List)r.id);
			return menu::Status::Running;

		case Kind::Screen:
			switch ((Screen)r.id)
			{
			case Screen::CalWet: to_cal_prompt(true); break;
			case Screen::CalDry: to_cal_prompt(false); break;
			case Screen::CalReset: to_cal_reset_prompt(); break;
			case Screen::Network: to_pairing(); break;
			case Screen::FactoryReset: to_reset_prompt(); break;
			}
			return menu::Status::Running;

		case Kind::Leave:
			if (list == List::Root)
			{
				return menu::Status::Exited;
			}
			to_list(LISTS[(int)list].parent);
			return menu::Status::Running;
		}
		return menu::Status::Running;
	}

} // namespace

void menu::enter()
{
	for (int &c : cursor)
	{
		c = 0;
	}
	list = List::Root;
	LOG_INF("[UI] menu opened");
	to_list(List::Root);
}

void menu::enter_network()
{
	for (int &c : cursor)
	{
		c = 0;
	}
	list = List::Root;
	to_pairing();
}

void menu::abort()
{
	go(State::List);
}

menu::Status menu::proceed(button::Event e)
{
	const int64_t now = k_uptime_get();

	switch (state)
	{
	case State::List:
	{
		int &sel = cursor[(int)list];
		if (e == button::Event::Short)
		{
			sel = (sel + 1) % visible_count();
			idle_at = now + MENU_IDLE_MS;
			draw(sel);
		}
		else if (e == button::Event::Long)
		{
			return activate(sel);
		}
		else if (now >= idle_at)
		{
			LOG_INF("[UI] menu idle timeout");
			return Status::Exited;
		}
		return Status::Running;
	}

	case State::CalPrompt:
		// The one screen where a tap is the safe choice: a capture in the wrong medium
		// cannot be told apart from a good one afterwards.
		if (e == button::Event::Long)
		{
			to_cal_result();
			return Status::Running;
		}
		if (e == button::Event::Short || now >= idle_at)
		{
			to_list(list);
		}
		return Status::Running;

	case State::CalResult:
		if (e != button::Event::None || now >= idle_at)
		{
			to_list(list);
		}
		return Status::Running;

	case State::CalResetPrompt:
		if (e == button::Event::Long)
		{
			prefs::set(prefs::CalDry, cfg::SOIL_MV_DRY);
			prefs::set(prefs::CalWet, cfg::SOIL_MV_WET);
			LOG_INF("[CAL] defaults restored: dry=%d mV  wet=%d mV",
					cfg::SOIL_MV_DRY, cfg::SOIL_MV_WET);
			to_list(list);
			return Status::Running;
		}
		if (e != button::Event::None || now >= idle_at)
		{
			to_list(list);
		}
		return Status::Running;

	case State::Pairing:
		if (pairing_qr)
		{
			/* The QR half. Scanned? The payoff is the readings, not a status page. Declined?
			 * Get on with the readings too. Either way the menu is done -- this is the one
			 * screen that exits to the sensor view directly. */
			if (net::commissioned())
			{
				LOG_INF("[NET] commissioned; on to the readings");
				return Status::Exited;
			}
			if (e != button::Event::None)
			{
				LOG_INF("[NET] onboarding code dismissed");
				return Status::Exited;
			}
			if (now >= idle_at)
			{
				idle_at = now + MENU_IDLE_MS; // re-arm the commissioner poll; never a timeout
			}
			return Status::Running;
		}
		// The CONNECTED half: any gesture or the timeout goes back to the list.
		if (e != button::Event::None || now >= idle_at)
		{
			to_list(list);
		}
		return Status::Running;

	case State::ResetPrompt:
		if (e == button::Event::Long)
		{
			LOG_INF("[UI] factory reset confirmed");
			return Status::FactoryReset;
		}
		if (e != button::Event::None || now >= idle_at)
		{
			to_list(list);
		}
		return Status::Running;
	}
	return Status::Exited; // unreachable; a new state must be handled above
}

int64_t menu::deadline_ms()
{
	return idle_at;
}
