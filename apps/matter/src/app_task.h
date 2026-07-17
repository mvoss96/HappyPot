#pragma once

#include <platform/CHIPDeviceLayer.h>

/** Brings up Matter, then hands the device itself to HappyPot.
 *
 * StartApp() runs the CHIP task queue on the main thread forever. Everything HappyPot does --
 * the panel, the probe, the button, the menu -- runs on a thread of its own (app::run(), see
 * src/app.hpp), because an e-paper refresh blocks for ~3 s, and it may not sit on the CHIP
 * event loop while OpenThread waits to poll its parent.
 *
 * Direction of travel: HappyPot measures, and hands each reading to Matter. Matter never
 * reaches back into the UI -- the network state is left where the loop picks it up
 * (net::set_link_up(), net::set_commissioned()).
 */
class AppTask {
public:
	static AppTask &Instance()
	{
		static AppTask sAppTask;
		return sAppTask;
	};

	CHIP_ERROR StartApp();

private:
	CHIP_ERROR Init();
};
