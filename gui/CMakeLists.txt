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

# QR codes: reading from the camera and showing an app's link (vendored
# zxing-cpp, linked statically).
find_package(Qt5 5.6 REQUIRED COMPONENTS Gui)
set(BUILD_WRITERS ON CACHE BOOL "" FORCE)
set(BUILD_READERS ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/3rdparty/zxing-cpp/core ${CMAKE_BINARY_DIR}/zxing EXCLUDE_FROM_ALL)
target_compile_options(ZXing PRIVATE -w)
set_target_properties(ZXing PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(harpoon-qr STATIC qr/qrdecoder.h qr/qrdecoder.cpp qr/qrencoder.h qr/qrencoder.cpp qr/qrimageprovider.h)
target_include_directories(harpoon-qr PUBLIC qr)
target_link_libraries(harpoon-qr PUBLIC Qt5::Gui PRIVATE ZXing::ZXing)

# The Sailfish app itself needs libsailfishapp and Silica, which only the
# SailfishOS SDK provides.
find_package(PkgConfig)
if(PkgConfig_FOUND)
    pkg_check_modules(SAILFISHAPP IMPORTED_TARGET sailfishapp)
endif()

if(SAILFISHAPP_FOUND)
    find_package(Qt5 5.6 REQUIRED COMPONENTS Qml Quick)
    add_executable(harpoon src/harpoon.cpp)
    target_link_libraries(harpoon PRIVATE harpoon-ui harpoon-qr Qt5::Qml Qt5::Quick PkgConfig::SAILFISHAPP)
    # The surface starts itself from Q_COREAPP_STARTUP_FUNCTION, so it is compiled
    # straight into the executable (a static library could drop it) and main() is
    # unchanged. 3rdparty/harbour-inspect is a byte-identical copy of sailfish-mcp's
    # harbour-inspect/ and is not kept in git.
    if(HARPOON_INSPECT_SURFACE)
        set(HARPOON_INSPECT_DIR ${CMAKE_SOURCE_DIR}/3rdparty/harbour-inspect)
        if(NOT EXISTS ${HARPOON_INSPECT_DIR}/inspectsurface.cpp)
            message(FATAL_ERROR "HARPOON_INSPECT_SURFACE needs sailfish-mcp's harbour-inspect/ copied to ${HARPOON_INSPECT_DIR}")
        endif()
        message(WARNING "inspect surface: ENABLED -- do not ship this build")
        target_sources(harpoon PRIVATE ${HARPOON_INSPECT_DIR}/inspectsurface.h ${HARPOON_INSPECT_DIR}/inspectsurface.cpp)
        target_include_directories(harpoon PRIVATE ${HARPOON_INSPECT_DIR})
        target_compile_definitions(harpoon PRIVATE INSPECT_SURFACE QT_MESSAGELOGCONTEXT)
        target_link_libraries(harpoon PRIVATE Qt5::Network)
    endif()
    install(TARGETS harpoon RUNTIME DESTINATION bin)
    install(DIRECTORY qml DESTINATION share/harpoon)
    install(FILES harpoon.desktop harpoon-autoupdate.desktop DESTINATION share/applications)
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
