add_executable(harpoon-cli main.cpp)
target_link_libraries(harpoon-cli PRIVATE harpoon-core)
install(TARGETS harpoon-cli RUNTIME DESTINATION bin)

# Periodic background check (systemd user units), enabled from the app's settings.
install(FILES ${CMAKE_SOURCE_DIR}/gui/systemd/harpoon-check.service ${CMAKE_SOURCE_DIR}/gui/systemd/harpoon-check.timer
        DESTINATION lib/systemd/user)
