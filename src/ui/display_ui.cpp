#include "display_ui.hpp"
#include "ui_platform.hpp"
#include "../app_config.hpp"
#include "../version.hpp"

#include <stdio.h>
#include <string.h>
#include <lvgl.h>

#include "ui_images/ui_images.h"

/* The one place that reaches into LVGL's Zephyr heap shim. Only the target build can
 * weigh the pool: the host sim runs LVGL on a big malloc heap, with 64-bit pointers
 * and 8-bit colour, so any figure it produced would be a plausible-sounding lie. */
#ifdef CONFIG_SYS_HEAP_RUNTIME_STATS
#include <zephyr/sys/mem_stats.h>
#include <lvgl_mem.h>
#endif

/* The big value face: a generated 1bpp subset of Montserrat 40 (digits + "% mV" only -- see
 * src/fonts/README.md). The built-in lv_font_montserrat_40 is ~70 KB of flash for glyphs the
 * value labels never draw. C linkage. */
extern "C"
{
	extern const lv_font_t montserrat_40_digits;
}

/*
 * HappyPot UI: one flat file, the airInk architecture on a quarter of the pixels. Exactly one
 * content view is visible at a time; each view is a lightweight container, so a view switch is
 * a single hide/show, and the VIEWS table below is the whole registry of them.
 *
 * E-paper refresh: full (blanking on/off -> clean, black flash) on arriving at a resting view
 * and periodically to clear ghosting; partial (fast, no flash) for in-place value updates and
 * everything the user steps through. See flush()/refresh() and plat::blanking_*.
 */

namespace
{
	constexpr int SCR_W = 200, SCR_H = 200;
	constexpr int FULL_REFRESH_EVERY = 100;

	/* The menu: a block of entries over a hint line. The static_assert is load-bearing -- LVGL
	 * happily paints labels over each other, so a row that no longer fits must fail the build. */
	constexpr int MENU_HINT_H = 24;
	constexpr int MENU_ITEM_H = 36;

	static_assert(ui::LIST_MAX_ROWS * MENU_ITEM_H + MENU_HINT_H <= SCR_H,
				  "the menu entries no longer fit above the hint -- shrink MENU_ITEM_H, or the "
				  "panel will draw them on top of each other");

	inline int menu_top(int rows)
	{
		return (SCR_H - MENU_HINT_H - rows * MENU_ITEM_H) / 2;
	}

	// Content views (exactly one un-hidden at a time).
	lv_obj_t *boot_root;
	lv_obj_t *sensor_root, *sensor_icon, *sensor_pct;
	lv_obj_t *error_root, *err_title_lbl, *err_detail_lbl;
	lv_obj_t *lowbat_root, *lowbat_pct_lbl;
	lv_obj_t *reset_root;
	/* THE menu. One set of widgets, and it draws whatever list it is handed -- it does not know
	 * that a root and a Calibrate sub-menu exist, because to a panel they are four strings and a
	 * cursor, twice. */
	lv_obj_t *list_root, *list_cursor;
	lv_obj_t *list_item[ui::LIST_MAX_ROWS];
	int list_rows = 0; // how many are in use right now; the rest are hidden
	int list_sel = -1; // -1 = nothing drawn yet

	/* The pairing view. The QR canvas is an I1 (1-bit indexed) draw buffer that lv_qrcode
	 * allocates from the LVGL pool when we hand it the payload -- so a build without codes
	 * never pays for it. */
	lv_obj_t *pair_root, *pair_qr_obj, *pair_code_lbl, *pair_state_lbl, *pair_hint_lbl;

	/* The calibration screens share the full-canvas artwork idiom of the sensor view: one image
	 * per screen (cal_wet / cal_dry / cal_reset / cal_done), a hint, and -- on the result -- the
	 * captured millivolts. */
	lv_obj_t *calib_root, *calib_img, *calib_value_lbl, *calib_hint_lbl;

	/* Views + refresh bookkeeping. Setters stage `pending_view` and dirty the
	 * widgets; ui::refresh() commits: hide/show the view + ONE panel refresh. */
	enum View
	{
		VIEW_NONE,
		VIEW_BOOT,
		VIEW_SENSOR,
		VIEW_ERROR,
		VIEW_LOWBAT,
		VIEW_RESET,
		VIEW_MENU,
		VIEW_PAIRING,
		VIEW_CALIB_PROMPT,
		VIEW_CALIB_RESET,
		VIEW_CALIB_RESULT,
		VIEW_COUNT
	};
	View shown_view = VIEW_NONE;   // what the panel currently shows
	View pending_view = VIEW_BOOT; // staged by a show_/set_<view>; committed by refresh()
	bool dirty;					   // a setter changed something since the last refresh
	int partials_since_full;
	bool ready; // init() built the widgets; every entry point no-ops until then

	unsigned design = 1; // which face family the sensor view draws (see icon_table)

	/* Skip-refresh dedup. int, not the API's narrower types, because -1 = "nothing yet". */
	int last_shown_percent = -1;
	const lv_image_dsc_t *last_icon_src;
	int last_lowbat_pct = -1;
	int last_calib_mv = INT32_MIN;

	/** [design][mood] -> image. Mood order: 0=thirsty, 1=meh, 2=happy. */
	const lv_image_dsc_t *const icon_table[2][3] = {
		{&s0_thirsty, &s0_meh, &s0_happy},
		{&s1_thirsty, &s1_meh, &s1_happy},
	};

	const lv_image_dsc_t *icon_for_percent(int percent)
	{
		int mood = (percent >= cfg::MOOD_HAPPY_PCT) ? 2
				   : (percent >= cfg::MOOD_MEH_PCT) ? 1
													: 0;
		unsigned d = (design < 2) ? design : 0;
		return icon_table[d][mood];
	}

	// ---- pool accounting ----

	/** Bytes currently allocated from the LVGL pool.
	 * @return the figure, or 0 where it cannot be measured (the host sim) */
	uint32_t heap_used()
	{
#ifdef CONFIG_SYS_HEAP_RUNTIME_STATS
		struct sys_memory_stats s{};
		lvgl_heap_stats(&s);
		return (uint32_t)s.allocated_bytes;
#else
		return 0;
#endif
	}

	/** Log what one builder cost the pool. Views are resident, so this is what it
	 * costs forever, not just during init(). */
	uint32_t log_built(const char *view, uint32_t before)
	{
		const uint32_t now = heap_used();
#ifdef CONFIG_SYS_HEAP_RUNTIME_STATS
		char line[48];
		snprintf(line, sizeof(line), "[LVGL] %-11s %5u B\n", view, (unsigned)(now - before));
		plat::log(line);
#else
		(void)view;
		(void)before;
#endif
		return now;
	}

	// ---- widget helpers ----

	/** Create a centred black label on a white plate (the plate is what keeps it readable
	 * over the full-canvas artwork). */
	lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_coord_t w)
	{
		lv_obj_t *l = lv_label_create(parent);
		lv_obj_set_style_text_font(l, font, 0);
		lv_obj_set_style_text_color(l, lv_color_black(), 0);
		lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
		lv_obj_set_style_bg_color(l, lv_color_white(), 0);
		lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
		lv_obj_set_style_pad_hor(l, 4, 0);
		lv_obj_set_style_pad_ver(l, 2, 0);
		if (w)
		{
			lv_obj_set_width(l, w);
		}
		return l;
	}

	/** Create a solid black rectangle, used as the menu cursor. */
	lv_obj_t *make_divider(lv_obj_t *parent, lv_coord_t w, lv_coord_t h)
	{
		lv_obj_t *d = lv_obj_create(parent);
		lv_obj_remove_style_all(d);
		lv_obj_set_size(d, w, h);
		lv_obj_set_style_bg_color(d, lv_color_black(), 0);
		lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
		return d;
	}

	/** Create a full-screen white content container. */
	lv_obj_t *make_view(lv_obj_t *parent)
	{
		lv_obj_t *c = lv_obj_create(parent);
		lv_obj_remove_style_all(c);
		lv_obj_set_size(c, SCR_W, SCR_H);
		lv_obj_set_pos(c, 0, 0);
		lv_obj_set_style_bg_color(c, lv_color_white(), 0);
		lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
		lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
		return c;
	}

	/** Push the current LVGL frame to the panel.
	 * The ssd16xx driver does a partial refresh whenever blanking is off; wrapping the
	 * flush in blanking on/off forces a full refresh instead.
	 *
	 * @param full true for a full refresh (clean, but a black flash), false for a
	 *             partial one (fast, only changed pixels, no flash)
	 */
	void flush(bool full)
	{
		lv_display_t *disp = lv_display_get_default();

		// Wake the panel for the whole refresh; the full-refresh update fires in
		// blanking_off(), so suspend only after that.
		plat::display_resume();
		if (full)
		{
			plat::blanking_on();  // select the full-refresh profile
			lv_refr_now(disp);	  // write RAM (no refresh yet)
			plat::blanking_off(); // trigger the full refresh
		}
		else
		{
			lv_refr_now(disp); // partial refresh
		}
		plat::display_suspend(); // deep-sleep the panel until the next refresh
	}

	/** Everything refresh() needs per view: where its container lands (address OF the pointer --
	 * they exist only after init(); null = a view this build lacks) and whether the user steps
	 * through it. `transient` views navigate with partial refreshes -- a full one is a ~2 s black
	 * flash -- and the ghosting is cleared by the full refresh on the way back to a resting view. */
	struct ViewDef
	{
		lv_obj_t **root;
		bool transient;
	};
	const ViewDef VIEWS[] = {
		/* VIEW_NONE         */ {&boot_root, false},
		/* VIEW_BOOT         */ {&boot_root, false},   // resting: up until the first reading
		/* VIEW_SENSOR       */ {&sensor_root, false}, // resting: the whole point of the device
		/* VIEW_ERROR        */ {&error_root, false},  // resting: up until the fault clears
		/* VIEW_LOWBAT       */ {&lowbat_root, false}, // resting: up until the battery is not
		/* VIEW_RESET        */ {&reset_root, true},   // a prompt, one button away from gone
		/* VIEW_MENU         */ {&list_root, true},	   // stepped through -- every menu, root or not
		/* VIEW_PAIRING      */ {&pair_root, true},	   // read and dismissed
		/* VIEW_CALIB_PROMPT */ {&calib_root, true},   // a prompt
		/* VIEW_CALIB_RESET  */ {&calib_root, true},   // a prompt
		/* VIEW_CALIB_RESULT */ {&calib_root, true},   // read for three seconds
	};
	static_assert(sizeof(VIEWS) / sizeof(VIEWS[0]) == (size_t)VIEW_COUNT,
				  "a new view must say where it lives and whether the user steps through it -- get "
				  "the second wrong and the panel flashes black on every step, or ghosts forever");

	bool transient(View v) { return VIEWS[v].transient; }
	lv_obj_t *root_for(View v) { return *VIEWS[v].root; }

	/** Show or hide one widget. */
	void set_hidden(lv_obj_t *o, bool hidden)
	{
		if (hidden)
		{
			lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
		}
		else
		{
			lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
		}
	}

	/** Hide every content view, so refresh() can un-hide exactly one. Driven off VIEWS. */
	void hide_all_content()
	{
		for (int v = VIEW_BOOT; v < VIEW_COUNT; v++)
		{
			if (lv_obj_t *root = root_for((View)v))
			{
				set_hidden(root, true);
			}
		}
	}

	// ---- builders (once, in init); `scr` is the active screen ----

	/** Build the boot splash: the artwork, the build's name, the version. */
	void build_boot(lv_obj_t *scr, const char *build)
	{
		boot_root = make_view(scr);

		lv_obj_t *art = lv_image_create(boot_root);
		lv_image_set_src(art, &boot);
		lv_obj_align(art, LV_ALIGN_CENTER, 0, 0);

		/* Which image is on the board. Two builds of this firmware exist and they look alike
		 * everywhere else; the one moment the panel can say so for free is here. */
		lv_obj_t *meta = make_label(boot_root, &lv_font_montserrat_16, lv_pct(100));
		char line[48];
		snprintf(line, sizeof(line), "%s\nv" HAPPYPOT_VERSION, build ? build : "");
		lv_label_set_text(meta, line);
		lv_obj_align(meta, LV_ALIGN_BOTTOM_MID, 0, -2);
	}

	/** Build the sensor view: the mood face with the percent on it. */
	void build_sensor(lv_obj_t *scr)
	{
		sensor_root = make_view(scr);

		sensor_icon = lv_image_create(sensor_root);
		lv_obj_align(sensor_icon, LV_ALIGN_CENTER, 0, 0);

		sensor_pct = make_label(sensor_root, &montserrat_40_digits, 0);
		lv_label_set_text(sensor_pct, "");
		lv_obj_align(sensor_pct, LV_ALIGN_BOTTOM_MID, 0, -4);
	}

	/** Build the error view: a title line and a detail line. */
	void build_error(lv_obj_t *scr)
	{
		error_root = make_view(scr);

		err_title_lbl = make_label(error_root, &lv_font_montserrat_16, SCR_W);
		lv_label_set_text(err_title_lbl, "");
		lv_obj_align(err_title_lbl, LV_ALIGN_CENTER, 0, -14);

		err_detail_lbl = make_label(error_root, &lv_font_montserrat_16, SCR_W);
		lv_label_set_text(err_detail_lbl, "");
		lv_obj_align(err_detail_lbl, LV_ALIGN_CENTER, 0, 14);
	}

	/** Build the low-battery warning: the artwork and the level. */
	void build_lowbat(lv_obj_t *scr)
	{
		lowbat_root = make_view(scr);

		lv_obj_t *art = lv_image_create(lowbat_root);
		lv_image_set_src(art, &lowbat);
		lv_obj_align(art, LV_ALIGN_CENTER, 0, 0);

		lowbat_pct_lbl = make_label(lowbat_root, &lv_font_montserrat_16, 0);
		lv_label_set_text(lowbat_pct_lbl, "");
		lv_obj_align(lowbat_pct_lbl, LV_ALIGN_BOTTOM_MID, 0, -2);
	}

	/** Build the factory-reset view. */
	void build_reset(lv_obj_t *scr)
	{
		reset_root = make_view(scr);

		lv_obj_t *title = make_label(reset_root, &lv_font_montserrat_16, SCR_W);
		lv_label_set_text(title, "FACTORY RESET");
		lv_obj_align(title, LV_ALIGN_CENTER, 0, -10);

		// Tap is the reflex, so tap is the harmless one. Same rule as the calibration prompts.
		lv_obj_t *hint = make_label(reset_root, &lv_font_montserrat_16, SCR_W);
		lv_label_set_text(hint, "Tap=cancel   Hold=reset");
		lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -6);
	}

	/** Build the menu: LIST_MAX_ROWS labels (always all of them -- cheaper than rebuilding the
	 * tree per list; show_list() hides the spares), a cursor, a hint. The cursor is created FIRST
	 * so the labels draw on top: white-on-black is the selection mark. */
	void build_list(lv_obj_t *scr)
	{
		list_root = make_view(scr);
		list_cursor = make_divider(list_root, SCR_W - 24, MENU_ITEM_H);

		for (int i = 0; i < ui::LIST_MAX_ROWS; i++)
		{
			list_item[i] = lv_label_create(list_root);
			lv_obj_set_style_text_font(list_item[i], &lv_font_montserrat_16, 0);
			lv_obj_set_style_text_color(list_item[i], lv_color_black(), 0);
			lv_obj_set_style_text_align(list_item[i], LV_TEXT_ALIGN_CENTER, 0);
			lv_obj_set_width(list_item[i], SCR_W - 24);
			lv_label_set_text(list_item[i], "");
		}

		lv_obj_t *hint = make_label(list_root, &lv_font_montserrat_16, SCR_W);
		lv_label_set_text(hint, "Tap=next   Hold=select");
		lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -2);
	}

	/** Build the pairing view: the QR with the manual code always under it (a camera is the
	 * thing most likely to fail, so the fallback is not behind a second screen). lv_qrcode
	 * allocates its I1 draw buffer from the LVGL pool -- the pool's largest single allocation. */
	void build_pairing(lv_obj_t *scr, const char *build, const char *qr, const char *manual)
	{
		pair_root = make_view(scr);

		lv_obj_t *hdr = make_label(pair_root, &lv_font_montserrat_16, SCR_W);
		lv_label_set_text(hdr, build ? build : "PAIRING");
		lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);

		/* Sized so the module grid lands on whole pixels: the Matter payload is ~22 chars,
		 * which QR encodes at 29x29 modules, and 145 = 5 px per module. A quiet zone is not
		 * drawn -- the panel around it is white, which is the same thing. */
		/* Sized so the module grid lands on whole pixels: the Matter payload is ~22 chars,
		 * which QR encodes at 29x29 modules, and 145 = 5 px per module. A quiet zone is not
		 * drawn -- the panel around it is white, which is the same thing. */
		pair_qr_obj = lv_qrcode_create(pair_root);
		lv_qrcode_set_size(pair_qr_obj, 145);
		lv_qrcode_set_dark_color(pair_qr_obj, lv_color_black());
		lv_qrcode_set_light_color(pair_qr_obj, lv_color_white());
		lv_qrcode_update(pair_qr_obj, qr, strlen(qr));
		lv_obj_align(pair_qr_obj, LV_ALIGN_TOP_MID, 0, 26);

		pair_code_lbl = make_label(pair_root, &lv_font_montserrat_16, SCR_W);
		lv_label_set_text(pair_code_lbl, manual);
		lv_obj_align(pair_code_lbl, LV_ALIGN_BOTTOM_MID, 0, -2);

		/* The other half of the same screen: once on a fabric there is nothing to scan, so the
		 * QR gives way to the state. Leaving the network is its own menu entry, not a gesture
		 * hidden on a screen that reads like a status line. */
		pair_state_lbl = make_label(pair_root, &lv_font_montserrat_16, SCR_W);
		lv_label_set_text(pair_state_lbl, "CONNECTED");
		lv_obj_align(pair_state_lbl, LV_ALIGN_CENTER, 0, -10);
		lv_obj_add_flag(pair_state_lbl, LV_OBJ_FLAG_HIDDEN);

		pair_hint_lbl = make_label(pair_root, &lv_font_montserrat_16, SCR_W);
		lv_label_set_text(pair_hint_lbl, "Tap to go back");
		lv_obj_align(pair_hint_lbl, LV_ALIGN_BOTTOM_MID, 0, -2);
		lv_obj_add_flag(pair_hint_lbl, LV_OBJ_FLAG_HIDDEN);
	}

	/** Build the shared calibration skeleton: full-canvas artwork, a value, a hint. */
	void build_calib(lv_obj_t *scr)
	{
		calib_root = make_view(scr);

		calib_img = lv_image_create(calib_root);
		lv_obj_align(calib_img, LV_ALIGN_CENTER, 0, 0);

		/* At the TOP, like the hint: the artwork's own caption (DONE) owns the bottom rows. */
		calib_value_lbl = make_label(calib_root, &montserrat_40_digits, 0);
		lv_label_set_text(calib_value_lbl, "");
		lv_obj_align(calib_value_lbl, LV_ALIGN_TOP_MID, 0, 2);
		lv_obj_add_flag(calib_value_lbl, LV_OBJ_FLAG_HIDDEN);

		/* At the TOP: the artwork carries its own caption (WET/DRY/RESET) at the bottom of the
		 * canvas, and a hint plate there would sit exactly on it. The top rows are empty. */
		calib_hint_lbl = make_label(calib_root, &lv_font_montserrat_16, SCR_W);
		lv_label_set_text(calib_hint_lbl, "");
		lv_obj_align(calib_hint_lbl, LV_ALIGN_TOP_MID, 0, 2);
	}

	/** Fill the calibration skeleton for one screen. */
	void calib_fill(const lv_image_dsc_t *img, const char *hint, bool with_value)
	{
		lv_image_set_src(calib_img, img);
		lv_label_set_text(calib_hint_lbl, hint ? hint : "");
		set_hidden(calib_hint_lbl, hint == nullptr);
		set_hidden(calib_value_lbl, !with_value);
	}

} // namespace

int ui::init(const Config &cfg)
{
	if (!plat::display_ready())
	{
		plat::log("Display not ready\n");
		return -1;
	}

	lv_obj_t *scr = lv_scr_act();
	lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
	lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

	// Weigh each view as it is built: they are all resident, so this is the standing
	// cost of the pool, and the number to look at before adding the next screen.
	uint32_t h = heap_used();
	build_boot(scr, cfg.build);
	h = log_built("boot", h);
	build_sensor(scr);
	h = log_built("sensor", h);
	build_error(scr);
	h = log_built("error", h);
	build_lowbat(scr);
	h = log_built("lowbat", h);
	build_reset(scr);
	h = log_built("reset", h);
	build_list(scr);
	h = log_built("menu", h);
	build_calib(scr);
	h = log_built("calib", h);
	/* Only when there is something to pair over -- otherwise no view, and the QR's draw buffer
	 * (the largest single allocation in the pool) is never made. */
	if (cfg.pair_qr)
	{
		build_pairing(scr, cfg.build, cfg.pair_qr, cfg.pair_manual ? cfg.pair_manual : "");
		log_built("pairing", h);
	}
	ready = true;

	// Boot splash: pending_view is VIEW_BOOT and shown is VIEW_NONE, so refresh()
	// paints it with a full refresh.
	ui::refresh();
	return 0;
}

void ui::show_pairing(bool commissioned)
{
	if (!ready || !pair_root)
	{
		return;
	}

	// Scan-me and already-on-the-network are the same screen with one half swapped out.
	const bool show_qr = !commissioned;
	set_hidden(pair_qr_obj, !show_qr);
	set_hidden(pair_code_lbl, !show_qr);
	set_hidden(pair_state_lbl, show_qr);
	set_hidden(pair_hint_lbl, show_qr);

	pending_view = VIEW_PAIRING;
	dirty = true;
}

void ui::show_sensor()
{
	if (!ready)
	{
		return;
	}
	pending_view = VIEW_SENSOR; // the widgets keep whatever set_sensor() last wrote
}

void ui::set_sensor(int32_t /*mv*/, int percent)
{
	if (!ready)
	{
		return;
	}

	const lv_image_dsc_t *want_icon = icon_for_percent(percent);
	if (percent == last_shown_percent && want_icon == last_icon_src)
	{
		return; // same displayed values already on the widgets
	}

	if (want_icon != last_icon_src)
	{
		lv_image_set_src(sensor_icon, want_icon);
		last_icon_src = want_icon;
	}

	char pct[16];
	snprintf(pct, sizeof(pct), "%d%%", percent);
	lv_label_set_text(sensor_pct, pct);

	last_shown_percent = percent;
	dirty = true;
}

void ui::set_low_battery(int percent)
{
	if (!ready)
	{
		return;
	}
	pending_view = VIEW_LOWBAT;

	if (percent < 0)
	{
		percent = 0;
	}
	if (percent > 100)
	{
		percent = 100;
	}
	if (percent == last_lowbat_pct)
	{
		return; // same value already on the widgets
	}

	char buf[8];
	snprintf(buf, sizeof(buf), "%d%%", percent);
	lv_label_set_text(lowbat_pct_lbl, buf);

	last_lowbat_pct = percent;
	dirty = true;
}

void ui::set_error(const char *title, const char *detail)
{
	if (!ready)
	{
		return;
	}
	pending_view = VIEW_ERROR;

	/* Deduped against the label text (which IS the record of the glass): a dead probe stages
	 * the same two lines every cycle, and refreshing for them would cost more than the idle
	 * floor in exactly the state where the battery must last until somebody looks. */
	if (strcmp(lv_label_get_text(err_title_lbl), title) != 0)
	{
		lv_label_set_text(err_title_lbl, title);
		dirty = true;
	}
	if (strcmp(lv_label_get_text(err_detail_lbl), detail) != 0)
	{
		lv_label_set_text(err_detail_lbl, detail);
		dirty = true;
	}
}

void ui::set_reset_prompt()
{
	if (!ready)
	{
		return;
	}
	pending_view = VIEW_RESET;
	dirty = true;
}

void ui::show_list(const char *const *labels, int count, int selected)
{
	if (!ready || count < 1 || count > LIST_MAX_ROWS)
	{
		return; // a list that does not fit is a bug, and drawing it over the hint would hide it
	}

	pending_view = VIEW_MENU;

	const int top = menu_top(count);

	// Re-place the block only when the row count changed -- which happens when a different list
	// comes up, and when a build has no Network row.
	if (count != list_rows)
	{
		for (int i = 0; i < LIST_MAX_ROWS; i++)
		{
			set_hidden(list_item[i], i >= count);
			lv_obj_set_pos(list_item[i], 12, top + i * MENU_ITEM_H + (MENU_ITEM_H - 18) / 2);
		}
		lv_obj_set_x(list_cursor, 12);
		list_rows = count;
		list_sel = -1; // force the cursor and the colours below
		dirty = true;
	}

	// The labels. lv_label already holds the last string, so it is also the record of what is on
	// the panel: comparing against it is what keeps an unchanged menu from costing a refresh.
	for (int i = 0; i < count; i++)
	{
		if (strcmp(lv_label_get_text(list_item[i]), labels[i]) != 0)
		{
			lv_label_set_text(list_item[i], labels[i]);
			dirty = true;
		}
	}

	if (selected != list_sel)
	{
		for (int i = 0; i < count; i++)
		{
			lv_obj_set_style_text_color(list_item[i],
										(i == selected) ? lv_color_white() : lv_color_black(), 0);
		}
		lv_obj_set_y(list_cursor, top + selected * MENU_ITEM_H);
		list_sel = selected;
		dirty = true;
	}
}

void ui::set_calib_prompt(bool wet)
{
	if (!ready)
	{
		return;
	}
	pending_view = VIEW_CALIB_PROMPT;
	calib_fill(wet ? &cal_wet : &cal_dry, "Hold=sample   Tap=back", false);
	dirty = true;
}

void ui::set_calib_reset_prompt()
{
	if (!ready)
	{
		return;
	}
	pending_view = VIEW_CALIB_RESET;
	calib_fill(&cal_reset, "Hold=defaults   Tap=back", false);
	dirty = true;
}

void ui::set_calib_result(int32_t mv)
{
	if (!ready)
	{
		return;
	}
	pending_view = VIEW_CALIB_RESULT;

	calib_fill(&cal_done, nullptr, true);
	if (mv != last_calib_mv)
	{
		char buf[16];
		snprintf(buf, sizeof(buf), "%ld mV", (long)mv);
		lv_label_set_text(calib_value_lbl, buf);
		last_calib_mv = mv;
	}
	dirty = true;
}

void ui::refresh()
{
	if (!ready)
	{
		return;
	}
	const bool view_changed = (pending_view != shown_view);
	if (!dirty && !view_changed)
	{
		return; // nothing changed since the last refresh
	}

	if (view_changed)
	{
		hide_all_content();
		lv_obj_clear_flag(root_for(pending_view), LV_OBJ_FLAG_HIDDEN);
	}

	// Full on the first paint, on arriving at a resting view (which also cleans up
	// after the transient ones), and periodically to clear ghosting. Everything the
	// user navigates through, and every in-place value update, is partial.
	const bool full = (shown_view == VIEW_NONE) ||
					  partials_since_full >= FULL_REFRESH_EVERY ||
					  (view_changed && !transient(pending_view));
	flush(full);

	partials_since_full = full ? 0 : partials_since_full + 1;
	shown_view = pending_view;
	dirty = false;
}

void ui::log_pool(const char *tag)
{
#ifdef CONFIG_SYS_HEAP_RUNTIME_STATS
	struct sys_memory_stats s{};
	lvgl_heap_stats(&s);

	char line[80];
	snprintf(line, sizeof(line), "[LVGL] %-12s used %u  peak %u  free %u  of %u B\n", tag,
			 (unsigned)s.allocated_bytes, (unsigned)s.max_allocated_bytes,
			 (unsigned)s.free_bytes, (unsigned)CONFIG_LV_Z_MEM_POOL_SIZE);
	plat::log(line);
#else
	(void)tag;
#endif
}
