#include "prefs.hpp"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <errno.h>
#include <stdio.h>

#include "app_config.hpp"
#include "sensors/soil.hpp" // soil::set_calibration -- the probe mirrors the store

LOG_MODULE_REGISTER(prefs, LOG_LEVEL_INF);

namespace
{
	int32_t cal_dry = cfg::SOIL_MV_DRY;
	int32_t cal_wet = cfg::SOIL_MV_WET;
	bool store_up = false;

	/* What has to happen once a setting changed, whoever changed it. Both calibration rows share
	 * this: the probe takes its endpoints in one go. A dry==wet pair maps every reading to 0 %,
	 * so it is refused here -- the store may hold it briefly (one endpoint saved, the other not
	 * yet), the probe never sees it. */
	void apply_cal(bool)
	{
		if (cal_dry == cal_wet)
		{
			LOG_WRN("calibration dry==wet (%d mV); the probe keeps its previous endpoints",
					cal_dry);
			return;
		}
		soil::set_calibration(cal_dry, cal_wet);
	}

	/* Every setting, once: key, address, valid range, and what happens when it changes. Loading,
	 * saving, clamping and applying all walk this table -- adding a setting is adding a row, not
	 * a new case in the loader plus a setter plus a forgettable aftermath. */
	struct Setting
	{
		const char *key;
		int32_t *value;
		int32_t lo, hi; // what a valid value is
		void (*apply)(bool local);
	};

	/* In prefs::Id order, and that order is the contract: C++ has no designated initialisers for
	 * arrays, so the static_assert below can catch a missing row but not a swapped one.
	 * The range is the SAADC's: 0..3600 mV covers the 1/5-gain channel's full scale. */
	const Setting SETTINGS[] = {
		/* CalDry */ {"cal_dry", &cal_dry, 0, 3600, apply_cal},
		/* CalWet */ {"cal_wet", &cal_wet, 0, 3600, apply_cal},
	};
	static_assert(ARRAY_SIZE(SETTINGS) == (size_t)prefs::COUNT, "a setting with no row");

	int32_t clamp(const Setting &s, int32_t v)
	{
		return v < s.lo ? s.lo : (v > s.hi ? s.hi : v);
	}

	const Setting *field_for(const char *name)
	{
		for (const Setting &s : SETTINGS)
		{
			if (settings_name_steq(name, s.key, nullptr))
			{
				return &s;
			}
		}
		return nullptr;
	}

	/* Called by settings_load_subtree() once per key found under "happypot". A key we do not
	 * know is not an error we should hide -- but it is not fatal either: it is what an older or
	 * newer firmware left behind. Return -ENOENT and the subsystem moves on. */
	int on_key(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
	{
		const Setting *s = field_for(name);
		if (!s)
		{
			return -ENOENT;
		}
		if (len != sizeof(int32_t))
		{
			return -EINVAL; // a record of the wrong width is a record from another firmware
		}
		const ssize_t n = read_cb(cb_arg, s->value, sizeof(int32_t));
		return (n < 0) ? (int)n : 0;
	}

	int save(const Setting &s)
	{
		if (!store_up)
		{
			return -ENODEV;
		}
		char key[32];
		snprintf(key, sizeof(key), "happypot/%s", s.key);
		const int err = settings_save_one(key, s.value, sizeof(int32_t));
		if (err)
		{
			LOG_WRN("%s NOT saved (%d)", s.key, err);
		}
		return err;
	}

	settings_handler handler = {
		.name = "happypot",
		.h_get = nullptr,
		.h_set = on_key,
		.h_commit = nullptr,
		.h_export = nullptr,
	};
} // namespace

int prefs::init()
{
	// Idempotent, and it has to be: in the Matter build the CHIP stack has already brought the
	// settings subsystem up for its fabrics by the time we get here, and in the BTHome build
	// nobody has. Zephyr returns 0 on the second call rather than failing, so both roads work.
	int err = settings_subsys_init();
	if (err)
	{
		LOG_WRN("settings unavailable (%d); calibration will not survive a reboot", err);
		return err;
	}

	err = settings_register(&handler);
	if (err && err != -EEXIST)
	{
		LOG_WRN("could not register (%d)", err);
		return err;
	}

	err = settings_load_subtree("happypot");
	if (err)
	{
		LOG_WRN("could not load (%d)", err);
		return err;
	}

	// Nothing stops a corrupt record, or one from another firmware, from arriving out of range.
	// Clamp on the way in, once, from the same bounds the setters use.
	for (const Setting &s : SETTINGS)
	{
		*s.value = clamp(s, *s.value);
	}

	store_up = true;
	LOG_INF("calibration dry=%d mV  wet=%d mV", cal_dry, cal_wet);
	return 0;
}

void prefs::apply_all()
{
	/* Walked, so a new row cannot be forgotten -- but a shared apply runs ONCE: both calibration
	 * rows are the probe's endpoints, and the probe takes them in one call. */
	void (*done[COUNT])(bool) = {};
	size_t n = 0;

	for (const Setting &s : SETTINGS)
	{
		bool already = false;
		for (size_t i = 0; i < n; i++)
		{
			already = already || (done[i] == s.apply);
		}
		if (already)
		{
			continue;
		}
		done[n++] = s.apply;
		s.apply(true);
	}
}

int32_t prefs::get(Id id)
{
	return *SETTINGS[id].value;
}

int32_t prefs::lo(Id id)
{
	return SETTINGS[id].lo;
}

int32_t prefs::hi(Id id)
{
	return SETTINGS[id].hi;
}

void prefs::set(Id id, int32_t v)
{
	const Setting &s = SETTINGS[id];

	v = clamp(s, v);
	if (v == *s.value)
	{
		return; // nothing moved: no flash write, no log
	}

	*s.value = v;
	save(s);
	s.apply(true);
}
