include_directories (${CHEWING_INCLUDE_DIR})
include_directories (${FCITX4_FCITX_INCLUDE_DIRS})
include_directories (${FCITX4_FCITX_CONFIG_INCLUDE_DIRS})
include_directories (${FCITX4_FCITX_UTILS_INCLUDE_DIRS})
include_directories (${PROJECT_BINARY_DIR})

set( fcitx_chewing_sources
    eim.c
    config.c
    )

add_definitions( -DLOCALEDIR=\"${CMAKE_INSTALL_PREFIX}/share/locale\" )

fcitx_add_addon_full(chewing
DESC
SOURCES ${fcitx_chewing_sources}
IM_CONFIG chewing.conf
LINK_LIBS ${CHEWING_LIBRARIES})