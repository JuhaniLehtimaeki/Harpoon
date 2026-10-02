cmake_minimum_required(VERSION 3.10)
project(Harpoon VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# The SailfishOS target ships Qt 5.6. Desktop builds use whatever Qt 5 is
# installed, so code must stick to APIs available in Qt 5.6.
find_package(Qt5 5.6 REQUIRED COMPONENTS Core Network DBus)

option(HARPOON_BUILD_TESTS "Build unit tests" ON)
if(HARPOON_BUILD_TESTS)
    enable_testing()
endif()
option(HARPOON_BUILD_TOOLS "Build developer tools (harpoon-probe)" ON)

add_subdirectory(core)
add_subdirectory(cli)
add_subdirectory(gui)

if(HARPOON_BUILD_TOOLS)
    add_subdirectory(tools/harpoon-probe)
endif()

if(HARPOON_BUILD_TESTS)
    add_subdirectory(core/tests)
endif()
