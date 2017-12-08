set(CHEWING_SOURCES
    eim.cpp
)
add_library(chewing MODULE ${CHEWING_SOURCES})
target_link_libraries(chewing Fcitx5::Core Fcitx5::Config PkgConfig::Chewing)
set_target_properties(chewing PROPERTIES PREFIX "")
target_compile_definitions(chewing PRIVATE FCITX_GETTEXT_DOMAIN=\"fcitx5-chinese-addons\")
install(TARGETS chewing DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5")
fcitx5_translate_desktop_file(chewing.conf.in chewing.conf)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/chewing.conf" DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/inputmethod")
fcitx5_translate_desktop_file(chewing-addon.conf.in chewing-addon.conf)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/chewing-addon.conf" RENAME chewing.conf DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon")
