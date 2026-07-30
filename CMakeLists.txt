cmake_minimum_required(VERSION 3.5)

project(untitled VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ✅ FIX: proper Qt detection
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets SerialPort Sql)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets SerialPort Sql)

set(PROJECT_SOURCES
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
)

qt_add_executable(untitled
    MANUAL_FINALIZATION
    ${PROJECT_SOURCES}
    master_communication.cpp
    master_communication.h
    master_frames.h master_frames.cpp
    databasemanager.h databasemanager.cpp
    audio/audiorecorder.h
    audio/audiorecorder.cpp
    gpsmanager.cpp
    gpsmanager.h

)

target_link_libraries(untitled PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::SerialPort
    Qt${QT_VERSION_MAJOR}::Sql
    asound
)

include(GNUInstallDirs)

install(TARGETS untitled
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(untitled)
endif()
