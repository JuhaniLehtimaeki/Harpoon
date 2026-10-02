# harpoon-ui: the QML-facing C++ layer. Plain QtCore so it builds and is
# tested on desktop Linux.
add_library(harpoon-ui STATIC
    src/applistmodel.h
    src/applistmodel.cpp
    src/harpooncontroller.h
    src/harpooncontroller.cpp
    src/harpoondbus.h
    src/harpoondbus.cpp
)
target_include_directories(harpoon-ui PUBLIC src)
target_link_libraries(harpoon-ui PUBLIC harpoon-core)

# The Sailfish app itself needs libsailfishapp and Silica, which only the
# SailfishOS SDK provides.
find_package(PkgConfig)
if(PkgConfig_FOUND)
    pkg_check_modules(SAILFISHAPP IMPORTED_TARGET sailfishapp)
endif()

if(SAILFISHAPP_FOUND)
    find_package(Qt5 5.6 REQUIRED COMPONENTS Qml Quick)
    add_executable(harpoon src/harpoon.cpp)
    target_link_libraries(harpoon PRIVATE harpoon-ui Qt5::Qml Qt5::Quick PkgConfig::SAILFISHAPP)
    install(TARGETS harpoon RUNTIME DESTINATION bin)
    install(DIRECTORY qml DESTINATION share/harpoon)
    install(FILES harpoon.desktop DESTINATION share/applications)
    foreach(size 86x86 108x108 128x128 172x172)
        install(FILES icons/${size}/harpoon.png DESTINATION share/icons/hicolor/${size}/apps)
    endforeach()
    install(FILES privileges/harpoon DESTINATION share/mapplauncherd/privileges.d)
    install(FILES dbus/io.github.juhanilehtimaeki.harpoon.service DESTINATION share/dbus-1/services)

    # translations/harpoon.ts is the source catalogue (engineering English);
    # add harpoon-<lang>.ts files for translations.
    file(GLOB HARPOON_TS translations/*.ts)
    find_package(Qt5LinguistTools QUIET)
    if(Qt5LinguistTools_FOUND)
        qt5_add_translation(HARPOON_QM ${HARPOON_TS})
    else()
        # Some SDK targets ship lrelease without the CMake package.
        find_program(HARPOON_LRELEASE NAMES lrelease lrelease-qt5 PATHS /usr/lib/qt5/bin /usr/lib64/qt5/bin)
        if(NOT HARPOON_LRELEASE)
            message(FATAL_ERROR "lrelease not found; install the Qt linguist tools")
        endif()
        foreach(ts ${HARPOON_TS})
            get_filename_component(base ${ts} NAME_WE)
            set(qm ${CMAKE_CURRENT_BINARY_DIR}/${base}.qm)
            add_custom_command(OUTPUT ${qm} COMMAND ${HARPOON_LRELEASE} -silent ${ts} -qm ${qm} DEPENDS ${ts})
            list(APPEND HARPOON_QM ${qm})
        endforeach()
    endif()
    add_custom_target(harpoon-translations ALL DEPENDS ${HARPOON_QM})
    install(FILES ${HARPOON_QM} DESTINATION share/harpoon/translations)
else()
    message(STATUS "sailfishapp not found: building harpoon-ui only (no GUI executable)")
endif()

if(HARPOON_BUILD_TESTS)
    add_subdirectory(tests)
endif()
