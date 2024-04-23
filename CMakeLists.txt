cmake_minimum_required(VERSION 3.6)
project(fcitx5-chewing VERSION 5.1.2)

find_package(ECM REQUIRED 1.0.0)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

find_package(PkgConfig REQUIRED)
find_package(Fcitx5Core 5.1.6 REQUIRED)
find_package(Fcitx5Module REQUIRED COMPONENTS TestFrontend)
find_package(Gettext REQUIRED)
include(FeatureSummary)
include(GNUInstallDirs)
include(ECMSetupVersion)
include(ECMUninstallTarget)

option(ENABLE_TEST "Build Test" On)
option(ENABLE_COVERAGE "Build the project with gcov support (Need ENABLE_TEST=On)" Off)

if (NOT DEFINED CHEWING_TARGET)
    pkg_check_modules(Chewing "chewing>=0.5.0" IMPORTED_TARGET REQUIRED)
    set(CHEWING_TARGET PkgConfig::Chewing)
endif()

include("${FCITX_INSTALL_CMAKECONFIG_DIR}/Fcitx5Utils/Fcitx5CompilerSettings.cmake")

add_subdirectory(src)
add_subdirectory(data)
add_subdirectory(po)

if (ENABLE_TEST)
    enable_testing()
    add_subdirectory(test)

    if (ENABLE_COVERAGE)
        add_custom_target(coverage
                        COMMAND "${CMAKE_CTEST_COMMAND}"
                        COMMAND lcov --gcov-tool "${GCOV_TOOL}" --no-external --capture --directory ./ -b "${CMAKE_CURRENT_SOURCE_DIR}" --output-file coverage.info
                        COMMAND genhtml coverage.info --output-directory "coverage_pages"
                        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    endif()
endif ()

fcitx5_translate_desktop_file(org.fcitx.Fcitx5.Addon.Chewing.metainfo.xml.in
                              org.fcitx.Fcitx5.Addon.Chewing.metainfo.xml XML)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/org.fcitx.Fcitx5.Addon.Chewing.metainfo.xml" DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
