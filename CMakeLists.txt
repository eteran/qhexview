cmake_minimum_required (VERSION 3.15)
project(QHexView)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 5.0.0 REQUIRED Widgets )

add_library(QHexView
    qhexview.cpp
    qhexview.h
    QHexView
)

target_include_directories (QHexView
PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(QHexView
PUBLIC
    Qt5::Widgets
)

set_target_properties(QHexView
    PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
