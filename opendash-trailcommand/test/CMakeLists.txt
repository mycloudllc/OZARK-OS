cmake_minimum_required(VERSION 3.10)
project(trailcommand_plugin_test CXX)
set(CMAKE_CXX_STANDARD 14)          # Dash builds with C++14
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
find_package(Qt5 COMPONENTS Widgets Network WebSockets REQUIRED)

# Real OpenDash headers (a checkout of https://github.com/openDsh/dash). Dash's CAN-bus header pulls in OpenAuto,
# so a one-line stand-in (stub/) replaces just that header for this test build.
set(DASH_INCLUDE "" CACHE PATH "Path to dash/include")
set(PLUGIN_DIR ${CMAKE_SOURCE_DIR}/../plugins/vehicle/trailcommand)
file(GLOB_RECURSE plugin_src ${PLUGIN_DIR}/*.cpp)   # same rule as Dash's plugins/CMakeLists.txt
add_library(trailcommand SHARED ${plugin_src})
target_include_directories(trailcommand PRIVATE ${CMAKE_SOURCE_DIR}/stub ${DASH_INCLUDE})
target_link_libraries(trailcommand Qt5::Widgets Qt5::Network Qt5::WebSockets)
set_target_properties(trailcommand PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins/vehicle)

add_executable(tctest host/tests.cpp)
target_include_directories(tctest PRIVATE ${CMAKE_SOURCE_DIR}/stub ${DASH_INCLUDE})
target_link_libraries(tctest Qt5::Widgets Qt5::Network)

add_executable(scantest host/scantest.cpp ${PLUGIN_DIR}/scanner.cpp)
target_include_directories(scantest PRIVATE ${PLUGIN_DIR})
target_link_libraries(scantest Qt5::Network)
