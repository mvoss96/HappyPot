#pragma once

/** @file
 * The firmware version, shown on the boot splash, in the console line at start-up, and in the
 * BTHome advert (obj 0xF2).
 *
 * The number itself is NOT here. It lives in <repo>/VERSION -- once, for both applications, for
 * the boot banner, and (in the Matter build) for the software version a controller reports under
 * Basic Information. Zephyr turns that file into APP_VERSION_STRING; cmake/happypot_version.cmake
 * is what points it at the shared file rather than at a per-app copy.
 *
 * The host preview (sim/) has no Zephyr and so no generated header: sim/build.ps1 reads the same
 * VERSION file and passes the string in via a generated header. Same source, two roads.
 */

#ifdef __ZEPHYR__
#include <zephyr/app_version.h>
#define HAPPYPOT_VERSION APP_VERSION_STRING
#elif !defined(HAPPYPOT_VERSION)
#error "Host build: HAPPYPOT_VERSION must come from <repo>/VERSION -- see sim/build.ps1."
#endif
