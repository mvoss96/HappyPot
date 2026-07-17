#pragma once
#include <stdint.h>
#include <zephyr/kernel.h>

/** @file
 * The single user button (P1.04), classified into two gestures.
 *
 * One button is the whole input vocabulary of this device, so the meaning has to come
 * from how long it is held. The rule, everywhere: a tap dismisses, a hold commits.
 *
 * The classification is Zephyr's: gpio-keys debounces and reports press/release, and the
 * input-longpress pseudo-device turns that into a short and a long code, the long one
 * delivered the moment the 700 ms threshold passes rather than on release (both in
 * dts/happypot_hw.dtsi). This module only maps those codes onto Event and hands them to
 * the loop one at a time. The callback runs in the input reporting context, so a loop
 * that blocks the system work queue loses button presses -- which is why the measurement
 * loop does not live there.
 */
namespace button
{
	enum class Event : uint8_t
	{
		None, // the wait timed out
		Short,
		Long
	};

	/** Start delivering button events.
	 *
	 * @retval 0       the gpio-keys device is ready
	 * @retval -ENODEV no button in the devicetree
	 */
	int init();

	/** Wait for a gesture, but no longer than a deadline.
	 *
	 * Presses that arrive while the caller is busy are queued, not lost, so a tap
	 * during a ~3 s e-paper refresh is still acted upon afterwards.
	 *
	 * @param deadline_ms absolute k_uptime_get() value to stop waiting at; a deadline
	 *                    already in the past polls without blocking
	 * @return the gesture, or Event::None if the deadline passed first
	 */
	Event wait_until(int64_t deadline_ms);

	/** The queue the gestures land in.
	 *
	 * For a caller that has to wait on more than the button and therefore cannot simply
	 * block in wait_until(). k_poll() this, then take the gesture with wait_until(0).
	 */
	k_msgq *queue();
}
