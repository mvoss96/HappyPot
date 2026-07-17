/**
 *    @file
 *          Project configuration overrides for CHIP.
 */

#pragma once

/**
 * Include a device name in the commissionable-node advertisement. Off by default, which is
 * why a commissioner has nothing to call the device before it can read Basic Information
 * and shows a generic placeholder ("Matter Accessory" in Home Assistant). Note this only
 * populates the DNS-SD commissionable record; over BLE the name a scanner sees is
 * CONFIG_BT_DEVICE_NAME. Whether a given controller surfaces either is up to the controller.
 */
#define CHIP_DEVICE_CONFIG_ENABLE_COMMISSIONABLE_DEVICE_NAME 1
#define CHIP_DEVICE_CONFIG_DEVICE_NAME "HappyPot"
