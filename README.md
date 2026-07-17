# HappyPot

A battery-powered capacitive soil-moisture sensor on an nRF52840 (ProMicro / nice!nano-class
SuperMini), with a 1.54" 200x200 SSD1681 e-paper showing a plant mood face, one button, and a
choice of radio. Modeled on [AirInk](../airInk)'s one-device / many-firmwares layout.

## The two applications

There is **one device** — `src/` (sensors, panel, button, menu, loop), `dts/happypot_hw.dtsi`
(the hardware), `conf/happypot_hw.conf` (its drivers) — and two firmwares built from it:

| App | Radio | Build system |
|---|---|---|
| `apps/bthome` | BLE **BTHome v2** broadcast (Home Assistant picks it up passively) | plain Zephyr |
| `apps/matter` | **Matter over Thread** (Soil Sensor device type 0x0045, SoilMeasurement cluster 0x0430, battery via Power Source on EP0) | sysbuild |

Everything the loop says to the outside world goes through `src/net.{hpp,cpp}` — a table of
hooks the app installs at boot (`apps/bthome/src/bthome_adv.cpp` or
`apps/matter/src/app_task.cpp`). The seam is deliberately radio-agnostic (a `Radio` descriptor,
`set_link_up()`, no compile switches in shared code) so a future `apps/zigbee` slots in without
touching `src/`.

## Building

NCS **v3.4.0**, toolchain `dcbdc366a1`. Board target is the **plain**
`promicro_nrf52840/nrf52840` (never the `/uf2` variant — the plain target links at 0x1000,
which is where the resident Adafruit UF2 bootloader starts a SoftDevice-less app).

```
west build -p -b promicro_nrf52840/nrf52840 apps/bthome -d apps/bthome/build
west build -p -b promicro_nrf52840/nrf52840 apps/matter -d apps/matter/build --sysbuild
```

Flash: copy `<build>/…/zephyr/zephyr.uf2` onto the bootloader's USB drive (double-tap reset).
A silent release build (logging off): append
`-- -DEXTRA_CONF_FILE=<abs>/conf/release.conf` (bthome) or
`-- -Dmatter_EXTRA_CONF_FILE=…` (matter).

## The flash map (and the bootloader incident)

`dts/happypot_flash.dtsi`, shared by both builds:

```
0x00000000  MBR                    4 KB
0x00001000  code                 940 KB
0x000EC000  storage               32 KB   settings/NVS: calibration + Matter fabrics
0x000F4000  UF2 bootloader        48 KB   RESERVED — no partition claims it
```

The board's stock devicetree put `storage_partition` **inside the bootloader region**, and the
old firmware's calibration NVS lived there (partition manager placed it at 0xFA000): every
calibration save wrote into bootloader flash. If a device that ran the old firmware no longer
enumerates its USB drive after a double-tap reset, that is why — reflash the bootloader over
SWD. `CONFIG_FLASH_LOAD_SIZE` in `conf/happypot_hw.conf` bounds the image so an overgrown build
fails loudly instead of silently landing on the storage or the bootloader.

**Upgrading from the old single-app firmware resets the calibration once** (the store moved from
raw NVS to the settings API at a safe offset). Recalibrate from the menu: hold the button, pick
*Calibrate*, sample *Wet point* (probe in water) and *Dry point* (probe in air).

## The button

One button (P1.04), two gestures, the airInk rule everywhere: **tap = next / dismiss, hold =
select / commit**. Hold opens the menu (Calibrate; Network and Factory reset appear only on a
build with a radio behind them). A factory-new Matter device boots straight into the pairing
screen — scan the QR (or type the manual code) from Home Assistant / any Matter controller.

## Matter notes

- Commissioning uses the **example attestation** (DAC FFF1/0x8000) and the **test passcode
  20202021** — pair with `enable_test_net_dcl` in Home Assistant's Matter server, or chip-tool.
- The Soil Sensor device type is Matter 1.5; a controller that does not surface it yet can
  still read it: `chip-tool soilmeasurement read soil-moisture-measured-value <node> 1`.
- No MCUboot, no OTA: field updates stay UF2 drag-drop.

## The host preview (sim/)

`./sim/build.ps1` (MSYS2 MinGW + the NCS LVGL checkout) compiles the real `src/ui/` against
LVGL on the PC and renders **every screen of both variants** to `sim/out/<variant>/*.png` — no
board, no flashing. The platform seam that makes this possible is `src/ui/ui_platform.hpp`
(Zephyr backend on the target, `sim/ui_platform_sim.cpp` on the host).

## Regenerating things

- UI artwork: `tools/svg_to_lvgl.py` (from `img/*.svg` into `src/ui_images/`).
- Matter data model: edit `apps/matter/src/default_zap/happypot.zap`, run
  `apps/matter/tools/zap_regen.ps1`, commit the generated files (NCS compiles them as-is).
- Version: edit `VERSION` (one file: boot banner, splash, BTHome advert, Matter software
  version, sim).
