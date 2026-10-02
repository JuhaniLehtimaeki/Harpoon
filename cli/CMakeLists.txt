add_executable(harpoon-cli main.cpp)
target_link_libraries(harpoon-cli PRIVATE harpoon-core)
install(TARGETS harpoon-cli RUNTIME DESTINATION bin)

# The background job: "harpoon-cli check --notify --quiet --auto-update" with
# no arguments or environment overrides, so it can be given the privileged
# group (gui/privileges/harpoon-autoupdate) for silent PackageKit installs.
add_executable(harpoon-autoupdate main.cpp)
target_link_libraries(harpoon-autoupdate PRIVATE harpoon-core)
target_compile_definitions(harpoon-autoupdate PRIVATE HARPOON_AUTOUPDATE_ONLY)
install(TARGETS harpoon-autoupdate RUNTIME DESTINATION bin)
