# - Try to find the CHEWING libraries
# Once done this will define
#
#  CHEWING_FOUND - system has CHEWING
#  CHEWING_INCLUDE_DIR - the CHEWING include directory
#  CHEWING_LIBRARIES - CHEWING library
#
# Copyright (c) 2010 Dario Freddi <drf@kde.org>
#
# Redistribution and use is allowed according to the terms of the BSD license.
# For details see the accompanying COPYING-CMAKE-SCRIPTS file.

if(CHEWING_INCLUDE_DIR AND CHEWING_LIBRARIES)
    # Already in cache, be silent
    set(CHEWING_FIND_QUIETLY TRUE)
endif(CHEWING_INCLUDE_DIR AND CHEWING_LIBRARIES)

find_package(PkgConfig)
pkg_check_modules(PC_LIBCHEWING QUIET chewing)

find_path(CHEWING_MAIN_INCLUDE_DIR
          NAMES chewing.h
          HINTS ${PC_LIBCHEWING_INCLUDEDIR}
          PATH_SUFFIXES chewing)

find_library(CHEWING_LIBRARIES
             NAMES chewing
             HINTS ${PC_LIBCHEWING_LIBDIR})

_pkgconfig_invoke("chewing" CHEWING DATADIR "" "--variable=datadir")

set(CHEWING_INCLUDE_DIR "${CHEWING_MAIN_INCLUDE_DIR}")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(CHEWING  DEFAULT_MSG  CHEWING_LIBRARIES CHEWING_MAIN_INCLUDE_DIR)

mark_as_advanced(CHEWING_INCLUDE_DIR CHEWING_LIBRARIES)
