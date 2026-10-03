cmake_minimum_required(VERSION 3.10)
project(Harpoon VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Position-independent executables (hardening; rpmlint asks for it).
if(POLICY CMP0083)
    cmake_policy(SET CMP0083 NEW)
    include(CheckPIESupported)
    check_pie_supported()
endif()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# The SailfishOS target ships Qt 5.6. Desktop builds use whatever Qt 5 is
# installed, so code must stick to APIs available in Qt 5.6.
find_package(Qt5 5.6 REQUIRED COMPONENTS Core Network DBus)

option(HARPOON_BUILD_TESTS "Build unit tests" ON)
if(HARPOON_BUILD_TESTS)
    enable_testing()
endif()
option(HARPOON_BUILD_TOOLS "Build developer tools (harpoon-probe)" ON)
# Debug builds only: compiles sailfish-mcp's inspect surface into the app so an
# agent can read and drive its UI on a phone. Never set for a package
# (rpm/harpoon.spec does not set it).
option(HARPOON_INSPECT_SURFACE "Build in the sailfish-mcp inspect surface (debug only)" OFF)

add_subdirectory(core)
add_subdirectory(cli)
add_subdirectory(gui)

if(HARPOON_BUILD_TOOLS)
    add_subdirectory(tools/harpoon-probe)
endif()

if(HARPOON_BUILD_TESTS)
    add_subdirectory(core/tests)
endif()
