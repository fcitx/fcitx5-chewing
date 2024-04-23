add_custom_target(copy-addon DEPENDS chewing-addon.conf.in-fmt)
add_custom_command(TARGET copy-addon COMMAND ${CMAKE_COMMAND} -E copy ${PROJECT_BINARY_DIR}/src/chewing-addon.conf ${CMAKE_CURRENT_BINARY_DIR}/chewing.conf)
