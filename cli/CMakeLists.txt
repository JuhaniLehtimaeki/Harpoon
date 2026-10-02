add_executable(harpoon-cli main.cpp)
target_link_libraries(harpoon-cli PRIVATE harpoon-core)
install(TARGETS harpoon-cli RUNTIME DESTINATION bin)
