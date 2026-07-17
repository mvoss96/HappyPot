#pragma once

/** @file
 * BTHome v2 over BLE legacy advertising -- this build's radio.
 *
 * The one file that speaks Bluetooth. It installs itself into the net:: seam
 * (net::set_hooks / net::set_radio); the shared loop then publishes through
 * net::publish_reading() without knowing what a BTHome is.
 */

/** Bring BLE up, create the advertising set and install the net:: hooks.
 * Call before app::run().
 *
 * @retval 0 ready
 * @retval <0 bt_enable or advertising-set creation failed
 */
int bthome_install(void);
