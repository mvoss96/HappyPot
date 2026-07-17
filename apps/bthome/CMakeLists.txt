cmake_minimum_required(VERSION 3.20.0)

set(HAPPYPOT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# The board's drivers, shared with the Matter build. Merged after prj.conf, so it holds board
# facts only -- see the file's header.
list(APPEND EXTRA_CONF_FILE ${HAPPYPOT_ROOT}/conf/happypot_hw.conf)

# The firmware version -- one file, <repo>/VERSION, for both apps and the panel. Must precede
# find_package(Zephyr): that is what loads version.cmake.
include(${HAPPYPOT_ROOT}/cmake/happypot_version.cmake)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(HappyPot)

include(${HAPPYPOT_ROOT}/cmake/happypot_sources.cmake)

target_sources(app PRIVATE
	src/main.cpp
	src/bthome_adv.cpp
	${HAPPYPOT_LOOP_SOURCES}
	${HAPPYPOT_DEVICE_SOURCES}
)

# So main.cpp and bthome_adv.cpp resolve "sensors/..." and "ui/..." the way sources inside
# src/ do.
target_include_directories(app PRIVATE ${HAPPYPOT_INCLUDE_DIR})

# Header-only BTHome v2 payload builder (git submodule). Only this app speaks BTHome, so only
# this app sees the library.
target_include_directories(app PRIVATE ${HAPPYPOT_ROOT}/lib/bthome-cpp/src)
