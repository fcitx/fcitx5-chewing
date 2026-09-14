install(DIRECTORY 16x16 22x22 24x24 48x48 scalable DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor"
         PATTERN ".*" EXCLUDE
         PATTERN "*~" EXCLUDE)

