#include "app.hpp"

#include "bthome_adv.hpp"

/** The BTHome firmware: install the radio's hooks, then run the device. */
int main(void)
{
	if (bthome_install() < 0)
	{
		return 0; // no radio is survivable; no device is not -- run() decides that
	}
	app::run("BTHome");
	return 0;
}
