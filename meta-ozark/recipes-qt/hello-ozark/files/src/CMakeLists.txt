cmake_minimum_required(VERSION 3.16)
project(hello-ozark VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 5.15 REQUIRED COMPONENTS Widgets)

add_executable(hello-ozark main.cpp resources.qrc)
target_link_libraries(hello-ozark PRIVATE Qt5::Widgets)

install(TARGETS hello-ozark RUNTIME DESTINATION bin)
