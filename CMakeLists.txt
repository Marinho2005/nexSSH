cmake_minimum_required(VERSION 3.16)
project(nexSSH VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 REQUIRED COMPONENTS Widgets)
find_package(qtermwidget5 CONFIG REQUIRED)
find_library(UTF8PROC_RUNTIME NAMES utf8proc libutf8proc.so.2 REQUIRED)
set_property(TARGET qtermwidget5 PROPERTY INTERFACE_LINK_LIBRARIES "Qt5::Widgets;${UTF8PROC_RUNTIME}")

add_executable(nexssh
    qt/src/main.cpp
    qt/src/ssh_config.cpp
    qt/src/ssh_config.hpp
    qt/resources.qrc
)

target_link_libraries(nexssh PRIVATE Qt5::Widgets qtermwidget5)
target_compile_definitions(nexssh PRIVATE SSH_DECK_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
