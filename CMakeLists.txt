# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(HappyPot)

target_sources(app PRIVATE
	src/main.cpp
	src/display_ui.cpp
	src/soil.cpp
	# Vendored SSD16XX display driver with PM support.
	# See src/drivers/README.md. Paired with CONFIG_SSD16XX=n.
	src/drivers/ssd16xx.c
)
