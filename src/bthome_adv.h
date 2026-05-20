#pragma once

#include <stdint.h>

/** Bring up the BLE stack as a non-connectable broadcaster. Call once at boot
 * after the kernel is up. Returns 0 on success, negative errno otherwise. */
int bthome_init(void);

/** Build a BTHome v2 advertisement (packet ID + moisture + battery + voltage)
 * using the bthome-cpp library, broadcast it for a short window across all
 * three advertising channels, then stop. Blocks ~300 ms.
 *
 * battery is a stub (100) until a battery monitor is wired up. */
int bthome_publish(int32_t mv, int percent);
