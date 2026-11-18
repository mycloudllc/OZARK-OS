cmake_minimum_required(VERSION 3.10)
project(ozark-remoted CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Core, DBus (BlueZ, Dash, the other services), WebSockets (Wi-Fi)
find_package(Qt5 COMPONENTS Core DBus Network WebSockets REQUIRED)

add_executable(ozark-remoted main.cpp remoted.cpp clients.cpp bridge.cpp ws.cpp ble.cpp)
target_compile_options(ozark-remoted PRIVATE -Wall -Wextra)
target_link_libraries(ozark-remoted Qt5::Core Qt5::DBus Qt5::Network Qt5::WebSockets)

include(GNUInstallDirs)
install(TARGETS ozark-remoted DESTINATION ${CMAKE_INSTALL_BINDIR})
