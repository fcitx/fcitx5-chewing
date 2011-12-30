include(FindPkgConfig)

PKG_CHECK_MODULES(CHEWING REQUIRED "chewing" )

include_directories (${CHEWING_INCLUDE_DIRS})
include_directories (${FCITX4_FCITX_INCLUDE_DIRS})
include_directories (${FCITX4_FCITX_CONFIG_INCLUDE_DIRS})
include_directories (${FCITX4_FCITX_UTILS_INCLUDE_DIRS})

set( fcitx_chewing_sources
    eim.c
    config.c
    )

add_definitions( -DLOCALEDIR=\"${CMAKE_INSTALL_PREFIX}/share/locale\" )

fcitx_add_addon(fcitx-chewing ${fcitx_chewing_sources} )
target_link_libraries (fcitx-chewing ${CHEWING_LIBRARIES})
