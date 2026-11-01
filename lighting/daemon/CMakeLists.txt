cmake_minimum_required(VERSION 3.10)
project(ozark-lightd CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 COMPONENTS Core Gui DBus SerialPort REQUIRED)

add_executable(ozark-lightd main.cpp lightd.cpp LedController.cpp AudioAnalyzer.cpp)
target_compile_options(ozark-lightd PRIVATE -Wall -Wextra)
# Gui only for QColor; no display is used
target_link_libraries(ozark-lightd Qt5::Core Qt5::Gui Qt5::DBus Qt5::SerialPort)

include(GNUInstallDirs)
install(TARGETS ozark-lightd DESTINATION ${CMAKE_INSTALL_BINDIR})
