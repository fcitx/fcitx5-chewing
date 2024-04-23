add_custom_target(copy-im DEPENDS chewing.conf.in-fmt)
add_custom_command(TARGET copy-im COMMAND ${CMAKE_COMMAND} -E copy ${PROJECT_BINARY_DIR}/src/chewing.conf ${CMAKE_CURRENT_BINARY_DIR}/chewing.conf)
