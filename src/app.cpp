#include "app.hpp"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>

#include "app_config.hpp"
#include "input/button.hpp"
#include "menu.hpp"
#include "net.hpp"
#include "prefs.hpp"
#include "sensors/battery.hpp"
#include "sensors/soil.hpp"
#include "ui/display_ui.hpp"
#include "version.hpp"

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

namespace
{
	const struct device *const log_uart = DEVICE_DT_GET(DT_NODELABEL(uart1));

	void set_log_uart_sleep(bool sleep)
	{
		if (!device_is_ready(log_uart))
		{
			return;
		}
		const pm_device_action action = sleep ? PM_DEVICE_ACTION_SUSPEND
											  : PM_DEVICE_ACTION_RESUME;
		(void)pm_device_action_run(log_uart, action);
	}

	/** Read both sensors, put the numbers on screen, tell the network.
	 *
	 * The low battery is a resting view of its own; the reading still goes out either way --
	 * this is a beacon device, and the battery level is part of what it beacons. */
	void measure()
	{
		net::Reading r{};
		if (auto err = soil::sample(&r.soil); err < 0)
		{
			LOG_ERR("soil sample failed (%d)", err);
			ui::set_error("PROBE ERROR", "soil read failed");
			return;
		}
		if (auto err = battery::sample(&r.battery); err < 0)
		{
			LOG_ERR("battery sample failed (%d)", err);
			ui::set_error("BATTERY ERROR", "ADC read failed");
			return;
		}

		LOG_INF("soil %4d mV  ~%3d %% | batt %4d mV  ~%3d %%",
				r.soil.mv, r.soil.percent, r.battery.mv, r.battery.percent);

		if (r.battery.percent <= cfg::LOW_BATTERY_PCT)
		{
			ui::set_low_battery(r.battery.percent);
		}
		else
		{
			ui::show_sensor();
			ui::set_sensor(r.soil.mv, r.soil.percent);
		}

		net::publish_reading(r);
	}

	/** The main app loop.
	 * @param menu_active true when run() opened the boot onboarding screen
	 *                    (menu::enter_network()) -- the loop then starts inside the menu,
	 *                    exactly as if the user had opened it */
	void app_loop(bool menu_active)
	{
		int64_t next_measure = k_uptime_get(); // the first one is due at once
		button::Event e = button::Event::None;

		while (true)
		{
			const int64_t now = k_uptime_get();

			set_log_uart_sleep(false);

			if (menu_active)
			{
				const menu::Status status = menu::proceed(e);
				if (status != menu::Status::Running)
				{
					if (status == menu::Status::FactoryReset && net::can_factory_reset())
					{
						// Leaves the screen as it is: the reset reboots us, and a device that
						// is about to lose its network should not spend its last second
						// drawing.
						LOG_INF("[UI] factory reset");
						net::factory_reset();
					}
					else
					{
						ui::show_sensor();
					}
					menu_active = false;
					next_measure = now;
				}
			}
			else if (e == button::Event::Long)
			{
				menu_active = true;
				menu::enter();
			}
			else if (now >= next_measure)
			{
				measure();
				next_measure = now + cfg::MEASUREMENT_MS;
			}

			// The one place the panel is committed. It paints only when a *displayed* value
			// changed -- the setters dedup on what the screen actually shows -- so most passes
			// end here without touching the e-paper. A refresh costs ~3 mAs.
			ui::refresh();

			// Whichever mode we are in has exactly one deadline. A deadline already in the
			// past makes the wait return at once, which is how leaving the menu gets its
			// reading without a second code path; the iteration that follows always pushes
			// next_measure, so this cannot spin.
			const int64_t wake_at = menu_active ? menu::deadline_ms() : next_measure;

			set_log_uart_sleep(true);
			e = button::wait_until(wake_at);

			if (e != button::Event::None)
			{
				set_log_uart_sleep(false);
				LOG_INF("[BTN] %s", (e == button::Event::Long) ? "hold" : "tap");
			}
		}
	}

} // namespace

void app::run(const char *build_name)
{
	set_log_uart_sleep(false);

	// What the panel needs. What the MENU offers is decided elsewhere -- it asks
	// net::has_radio() and net::can_factory_reset() -- because a row exists when there is
	// something behind it.
	const net::Radio *radio = net::radio();
	const ui::Config ui_cfg = {
		.build = build_name,
		.pair_qr = radio ? radio->pair_qr : nullptr,
		.pair_manual = radio ? radio->pair_manual : nullptr,
	};

	// Before the probe, because prefs only loads here. A store that will not come up is
	// survivable: getters answer the compile-time defaults and say so in the log.
	prefs::init();

	const bool display_ok = (ui::init(ui_cfg) == 0);

	LOG_INF("HappyPot v%s %s started (display %s)", HAPPYPOT_VERSION, build_name,
			display_ok ? "ok" : "FAILED");

	if (soil::init() < 0)
	{
		LOG_ERR("soil probe not ready");
		ui::set_error("PROBE ERROR", "soil probe not found");
		ui::refresh();
		k_sleep(K_FOREVER); // leave the error on screen
	}

	/* The stored calibration, put where it acts -- the first moment it can be, since the probe
	 * did not exist until now; the splash is still up, so nobody sees an uncalibrated reading. */
	prefs::apply_all();

	if (battery::init() < 0)
	{
		LOG_WRN("battery ADC not ready (continuing without it)");
	}
	if (button::init() < 0)
	{
		LOG_WRN("button not ready (continuing without it)");
	}
	ui::log_pool("boot splash"); // every view is built; this is the resident cost

	/* A device that has never joined anything boots straight into the pairing screen: the QR is
	 * the one thing a factory-new device is for, and the menu machinery already holds a screen
	 * without measuring. It leaves like any menu -- scanned or dismissed, on to the readings. */
	const bool show_code = net::has_radio() && net::radio()->pair_qr && !net::commissioned();
	if (show_code)
	{
		LOG_INF("[NET] not commissioned; the panel shows the onboarding code");
		menu::enter_network();
	}
	app_loop(show_code);
}
