find_package(Qt5 5.6 REQUIRED COMPONENTS Test)

set(CORE_TESTS_DIR ${CMAKE_SOURCE_DIR}/core/tests)

add_executable(tst_controller tst_controller.cpp ${CORE_TESTS_DIR}/minihttpserver.h)
target_include_directories(tst_controller PRIVATE ${CORE_TESTS_DIR})
target_link_libraries(tst_controller PRIVATE harpoon-ui Qt5::Test)
target_compile_definitions(tst_controller PRIVATE HARPOON_FIXTURE_DIR="${CORE_TESTS_DIR}/fixtures")
add_test(NAME tst_controller COMMAND tst_controller)

# QML smoke test: needs Qt Quick on the build machine (not the Silica SDK).
find_package(Qt5 5.6 QUIET COMPONENTS Quick Qml)
if(Qt5Quick_FOUND)
    add_executable(tst_qmlsmoke tst_qmlsmoke.cpp)
    target_include_directories(tst_qmlsmoke PRIVATE ${CORE_TESTS_DIR})
    target_link_libraries(tst_qmlsmoke PRIVATE harpoon-ui Qt5::Quick Qt5::Qml Qt5::Test)
    target_compile_definitions(tst_qmlsmoke PRIVATE
        HARPOON_FIXTURE_DIR="${CORE_TESTS_DIR}/fixtures"
        HARPOON_STUB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/silica-stub"
        HARPOON_QML_DIR="${CMAKE_SOURCE_DIR}/gui/qml")
    add_test(NAME tst_qmlsmoke COMMAND tst_qmlsmoke)
    set_tests_properties(tst_qmlsmoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# Compile (not run) the real entry point against a stand-in sailfishapp.h.
if(Qt5Quick_FOUND AND NOT SAILFISHAPP_FOUND)
    add_executable(harpoon-main-compile-check ${CMAKE_SOURCE_DIR}/gui/src/harpoon.cpp)
    target_include_directories(harpoon-main-compile-check PRIVATE stub-include)
    target_link_libraries(harpoon-main-compile-check PRIVATE harpoon-ui Qt5::Quick Qt5::Qml)
endif()
