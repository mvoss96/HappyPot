#include "button.hpp"

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device_runtime.h>

namespace
{
	// The gpio-keys hardware; init() gates on its readiness. The tap/hold split itself
	// belongs to the input-longpress pseudo-device below.
	const struct device *const btn_dev = DEVICE_DT_GET(DT_PATH(buttons));

	// The input-longpress device (dts/happypot_hw.dtsi) re-emits the raw press/release as
	// two derived codes. We listen to it, not to the raw button, so the queue only ever
	// sees finished gestures.
	const struct device *const lp_dev = DEVICE_DT_GET(DT_NODELABEL(longpress));

	/* A queue, not a k_event: two taps in quick succession are two gestures, and an
	 * event bit would silently merge them. Four deep is more than a human produces
	 * during the longest thing we block on (a ~3 s e-paper refresh). */
	K_MSGQ_DEFINE(gesture_q, sizeof(uint8_t), 4, 4);

	/** Map a longpress code to a gesture and queue it.
	 *
	 * Runs in the input reporting context (CONFIG_INPUT_MODE_SYNCHRONOUS: the gpio-keys
	 * debounce work item), so it only enqueues -- never blocks, never touches the panel.
	 *
	 * The long code arrives on its press edge, the instant the hold threshold passes; the
	 * short code arrives as a press/release pair on release. Either way we act on the
	 * press edge and ignore the rest.
	 *
	 * @param ev   the input event; anything but our two derived codes is ignored
	 * @param user unused
	 */
	void on_input(struct input_event *ev, void *user)
	{
		ARG_UNUSED(user);

		if (ev->type != INPUT_EV_KEY || !ev->value)
		{
			return;
		}

		uint8_t kind;
		if (ev->code == INPUT_KEY_MENU)
		{
			kind = (uint8_t)button::Event::Long;
		}
		else if (ev->code == INPUT_KEY_ENTER)
		{
			kind = (uint8_t)button::Event::Short;
		}
		else
		{
			return;
		}

		k_msgq_put(&gesture_q, &kind, K_NO_WAIT); // full queue: drop, never block
	}

	INPUT_CALLBACK_DEFINE(lp_dev, on_input, nullptr);

} // namespace

int button::init()
{
	if (!device_is_ready(btn_dev))
	{
		return -ENODEV;
	}

	/* Claim the button, and never let go: with CONFIG_PM_DEVICE_RUNTIME, gpio-keys enables runtime
	 * PM on itself and is thereby SUSPENDED from boot -- suspend disables the pin interrupt -- so
	 * without this reference the button is silently dead. No matching put(), on purpose: this is
	 * the panel's only input. (No-op when runtime PM is off.) */
	const int err = pm_device_runtime_get(btn_dev);
	if (err < 0)
	{
		printk("Button: could not resume gpio-keys (%d); the button will not report\n", err);
		return err;
	}

	return 0;
}

k_msgq *button::queue()
{
	return &gesture_q;
}

button::Event button::wait_until(int64_t deadline_ms)
{
	const int64_t now = k_uptime_get();
	const k_timeout_t timeout = (deadline_ms <= now) ? K_NO_WAIT : K_MSEC(deadline_ms - now);

	uint8_t kind;
	if (k_msgq_get(&gesture_q, &kind, timeout) != 0)
	{
		return Event::None;
	}
	return (Event)kind;
}
