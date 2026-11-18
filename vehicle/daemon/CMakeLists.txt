cmake_minimum_required(VERSION 3.10)
project(ozark-vehicled CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Only Core, DBus and Network: serial, CAN, RFCOMM and GPIO go through the
# kernel directly, BLE through BlueZ's D-Bus API
find_package(Qt5 COMPONENTS Core DBus Network REQUIRED)

set(SOURCES main.cpp vehicled.cpp obd.cpp dtc_names.cpp transport.cpp elm327.cpp cantransport.cpp
    gpio.cpp tpms.cpp hub.cpp)

add_executable(ozark-vehicled ${SOURCES})
target_compile_options(ozark-vehicled PRIVATE -Wall -Wextra)
target_link_libraries(ozark-vehicled Qt5::Core Qt5::DBus Qt5::Network)

include(GNUInstallDirs)
install(TARGETS ozark-vehicled DESTINATION ${CMAKE_INSTALL_BINDIR})

# Host tests: a fake ELM327 on a pty, and the decoders (not installed)
option(VEHICLED_TESTS "Build the host tests" OFF)
if(VEHICLED_TESTS)
    add_executable(vehicled-tests tests/tests.cpp obd.cpp dtc_names.cpp transport.cpp elm327.cpp
        cantransport.cpp tpms.cpp)
    target_include_directories(vehicled-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(vehicled-tests Qt5::Core Qt5::DBus)
endif()
