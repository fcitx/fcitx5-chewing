install(DIRECTORY 16x16 22x22 24x24 48x48 scalable DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor"
         PATTERN ".*" EXCLUDE
         PATTERN "*~" EXCLUDE)

install(FILES smart-english/smart-english-words.txt
              smart-english/smart-english-proper-nouns.txt
              smart-english/smart-english-char-ngrams.txt
        DESTINATION "${FCITX_INSTALL_PKGDATADIR}/chewing")

