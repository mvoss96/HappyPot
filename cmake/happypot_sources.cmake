# The HappyPot device, as source lists. Both applications (apps/bthome, apps/matter) include
# this file, for the same reason they share dts/happypot_hw.dtsi and conf/happypot_hw.conf: there
# is one device, and a file that only one of them compiles is a file the other silently lacks.
#
# Split in two, mirroring airInk:
#
#   HAPPYPOT_DEVICE_SOURCES  the sensors, the panel, the generated images, the vendored driver.
#                            Everything that talks to hardware.
#   HAPPYPOT_LOOP_SOURCES    the measurement loop and the seam to whatever network the build
#                            brings. The firmware -- both builds of it -- runs exactly this.

set(HAPPYPOT_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
set(HAPPYPOT_INCLUDE_DIR ${HAPPYPOT_ROOT}/src)

set(HAPPYPOT_DEVICE_SOURCES
	${HAPPYPOT_ROOT}/src/sensors/soil.cpp
	${HAPPYPOT_ROOT}/src/sensors/battery.cpp
	${HAPPYPOT_ROOT}/src/ui/display_ui.cpp
	${HAPPYPOT_ROOT}/src/ui/ui_platform.cpp
	# Generated 1bpp digit subset for the big value labels (see src/fonts/README.md);
	# the full built-in Montserrat 40 costs ~70 KB the Matter image does not have.
	${HAPPYPOT_ROOT}/src/fonts/montserrat_40_digits.c
	# Vendored SSD16XX display driver with PM_DEVICE deep-sleep support.
	# See src/drivers/README.md. Paired with CONFIG_SSD16XX=n.
	${HAPPYPOT_ROOT}/src/drivers/ssd16xx.c
	# Generated 1-bit LVGL image arrays. Regenerate with tools/svg_to_lvgl.py.
	${HAPPYPOT_ROOT}/src/ui_images/boot.c
	${HAPPYPOT_ROOT}/src/ui_images/cal_done.c
	${HAPPYPOT_ROOT}/src/ui_images/cal_dry.c
	${HAPPYPOT_ROOT}/src/ui_images/cal_reset.c
	${HAPPYPOT_ROOT}/src/ui_images/cal_wet.c
	${HAPPYPOT_ROOT}/src/ui_images/lowbat.c
	${HAPPYPOT_ROOT}/src/ui_images/s0_happy.c
	${HAPPYPOT_ROOT}/src/ui_images/s0_meh.c
	${HAPPYPOT_ROOT}/src/ui_images/s0_thirsty.c
	${HAPPYPOT_ROOT}/src/ui_images/s1_happy.c
	${HAPPYPOT_ROOT}/src/ui_images/s1_meh.c
	${HAPPYPOT_ROOT}/src/ui_images/s1_thirsty.c
)

set(HAPPYPOT_LOOP_SOURCES
	${HAPPYPOT_ROOT}/src/app.cpp
	${HAPPYPOT_ROOT}/src/net.cpp
	${HAPPYPOT_ROOT}/src/menu.cpp
	${HAPPYPOT_ROOT}/src/prefs.cpp
	${HAPPYPOT_ROOT}/src/input/button.cpp
)

# Generated LVGL image arrays use a guarded include block that falls back to
# "lvgl/lvgl.h" unless one of LV_LVGL_H_INCLUDE_* is defined. Pick SIMPLE so
# they resolve to plain "lvgl.h", which Zephyr's LVGL module exposes.
zephyr_compile_definitions(LV_LVGL_H_INCLUDE_SIMPLE)
