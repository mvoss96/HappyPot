#pragma once

#include <stdint.h>

#include <zephyr/sys/util.h>

/** @file
 * The single-cell Li-Ion open-circuit curve, piecewise-linearized -- shared knowledge, not
 * state, so it lives in a header the way airInk keeps its curve.
 */
namespace battery_curve
{
	struct Point
	{
		int32_t mv;
		int pct;
	};

	/** Typical single-cell Li-Ion open-circuit curve. */
	constexpr Point kCurve[] = {
		{3300, 0},
		{3600, 5},
		{3680, 10},
		{3740, 20},
		{3770, 30},
		{3790, 40},
		{3820, 50},
		{3870, 60},
		{3920, 70},
		{4000, 80},
		{4110, 90},
		{4200, 100},
	};

	/** Map an open-circuit cell voltage to a rough state of charge. */
	constexpr int mv_to_percent(int32_t mv)
	{
		if (mv <= kCurve[0].mv)
		{
			return 0;
		}
		if (mv >= kCurve[ARRAY_SIZE(kCurve) - 1].mv)
		{
			return 100;
		}

		for (size_t i = 1; i < ARRAY_SIZE(kCurve); ++i)
		{
			if (mv <= kCurve[i].mv)
			{
				const Point lo = kCurve[i - 1];
				const Point hi = kCurve[i];
				const int32_t num = (mv - lo.mv) * (hi.pct - lo.pct);
				const int32_t den = (hi.mv - lo.mv);
				return lo.pct + static_cast<int>(num / den);
			}
		}

		return 100;
	}
}
