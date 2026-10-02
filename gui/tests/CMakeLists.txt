find_package(Qt5 5.6 REQUIRED COMPONENTS Test)

set(CORE_TESTS_DIR ${CMAKE_SOURCE_DIR}/core/tests)

add_executable(tst_controller tst_controller.cpp ${CORE_TESTS_DIR}/minihttpserver.h)
target_include_directories(tst_controller PRIVATE ${CORE_TESTS_DIR})
target_link_libraries(tst_controller PRIVATE harpoon-ui Qt5::Test)
target_compile_definitions(tst_controller PRIVATE HARPOON_FIXTURE_DIR="${CORE_TESTS_DIR}/fixtures")
add_test(NAME tst_controller COMMAND tst_controller)
