cmake_minimum_required(VERSION 3.6)
project(fcitx5-chewing VERSION 5.1.1)

find_package(ECM REQUIRED 1.0.0)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

find_package(PkgConfig REQUIRED)
find_package(Fcitx5Core 5.0.2 REQUIRED)
find_package(Gettext REQUIRED)
include(FeatureSummary)
include(GNUInstallDirs)
include(ECMSetupVersion)
include(ECMUninstallTarget)

if (NOT DEFINED CHEWING_TARGET)
    pkg_check_modules(Chewing "chewing>=0.5.0" IMPORTED_TARGET REQUIRED)
    set(CHEWING_TARGET PkgConfig::Chewing)
endif()

include("${FCITX_INSTALL_CMAKECONFIG_DIR}/Fcitx5Utils/Fcitx5CompilerSettings.cmake")

add_subdirectory(src)
add_subdirectory(data)
add_subdirectory(po)

fcitx5_translate_desktop_file(org.fcitx.Fcitx5.Addon.Chewing.metainfo.xml.in
                              org.fcitx.Fcitx5.Addon.Chewing.metainfo.xml XML)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/org.fcitx.Fcitx5.Addon.Chewing.metainfo.xml" DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
