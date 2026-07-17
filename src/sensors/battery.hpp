#pragma once

#include <stdint.h>

/** One smoothed reading of the cell. */
struct BatteryReading
{
	int32_t mv;
	int percent;
};

/** @file
 * Battery monitor on the SAADC's internal VDDH/5 path -- no external divider, no divider drain.
 * The module owns its ADC channel (devicetree, zephyr,user index 1) and smooths over the last
 * few samples; the Li-Ion curve lives in battery_curve.hpp.
 */
namespace battery
{
	/** Bring up the ADC channel.
	 * @retval 0 ready; negative = no channel on this board */
	int init();

	/** Read the cell, push it into the rolling window, return the smoothed value via *out.
	 * @retval 0 ok; negative = ADC error */
	int sample(BatteryReading *out);
}
