# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(HappyPot)

# Generated LVGL image arrays use a guarded include block that falls back to
# "lvgl/lvgl.h" unless one of LV_LVGL_H_INCLUDE_* is defined. Pick SIMPLE so
# they resolve to plain "lvgl.h", which Zephyr's LVGL module exposes.
zephyr_compile_definitions(LV_LVGL_H_INCLUDE_SIMPLE)

target_sources(app PRIVATE
	src/main.cpp
	src/display_ui.cpp
	src/soil.cpp
	src/battery.cpp
	src/bthome_adv.cpp
	# Vendored SSD16XX display driver with PM support.
	# See src/drivers/README.md. Paired with CONFIG_SSD16XX=n.
	src/drivers/ssd16xx.c
	# Generated 1-bit LVGL image arrays. Regenerate with tools/svg_to_lvgl.py.
	src/ui_images/boot.c
	src/ui_images/s0_happy.c
	src/ui_images/s0_meh.c
	src/ui_images/s0_thirsty.c
	src/ui_images/s1_happy.c
	src/ui_images/s1_meh.c
	src/ui_images/s1_thirsty.c
)

# Header-only BTHome v2 payload builder (git submodule).
target_include_directories(app PRIVATE lib/bthome-cpp/src)
