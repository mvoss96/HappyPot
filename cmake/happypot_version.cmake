# The firmware version, edited in exactly one place: <repo>/VERSION.
#
# Zephyr wants that file inside the application directory, and it is not negotiable: the rule that
# generates app_version.h is written as
#
#     if(EXISTS ${APPLICATION_SOURCE_DIR}/VERSION)
#       add_custom_command(... -DVERSION_FILE=${APPLICATION_SOURCE_DIR}/VERSION ...)
#
# (zephyr/CMakeLists.txt). Setting VERSION_FILE ourselves does not move it -- that variable only
# feeds the CMake-side version numbers; the generated header stays hardcoded to the application
# directory. So each app needs its own VERSION file.
#
# It needs to *have* one. It does not need to *keep* one. This copies the single source into place
# before find_package(Zephyr) runs -- which is when Zephyr looks -- and .gitignore keeps the copies
# out of the repository, so there is nothing to edit twice and nothing to drift.
#
# From that one number come: the boot banner, APP_VERSION_STRING (which src/version.hpp hands to
# the panel, the start-up log and the BTHome advert), and -- in the Matter build -- the software
# version a controller shows under Basic Information. The host preview has no Zephyr and reads
# <repo>/VERSION itself; see sim/build.ps1.
#
# Include this BEFORE find_package(Zephyr).

if(NOT EXISTS ${HAPPYPOT_ROOT}/VERSION)
    message(FATAL_ERROR "No VERSION file at ${HAPPYPOT_ROOT} -- it is the one place the version lives.")
endif()

# COPYONLY, and CMake only rewrites the file when the contents differ, so this does not churn the
# build on every configure.
configure_file(${HAPPYPOT_ROOT}/VERSION ${CMAKE_CURRENT_SOURCE_DIR}/VERSION COPYONLY)
