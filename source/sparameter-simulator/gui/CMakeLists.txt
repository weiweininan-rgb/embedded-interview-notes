cmake_minimum_required(VERSION 3.21)
project(sparam_gui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Charts Test)
qt_standard_project_setup()
enable_testing()

qt_add_executable(sparam_gui WIN32
    main.cpp
    mainwindow.cpp
    mainwindow.h
    protocolclient.cpp
    protocolclient.h
    theme.cpp
    theme.h
)

target_link_libraries(sparam_gui PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::Charts
)

set_target_properties(sparam_gui PROPERTIES
    WIN32_EXECUTABLE TRUE
)

qt_add_executable(test_gui_reconnect
    tests/test_gui_reconnect.cpp
    mainwindow.cpp
    mainwindow.h
    protocolclient.cpp
    protocolclient.h
    theme.cpp
    theme.h
)

target_link_libraries(test_gui_reconnect PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::Charts
    Qt6::Test
)
target_include_directories(test_gui_reconnect PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(test_gui_reconnect PROPERTIES WIN32_EXECUTABLE FALSE)

add_test(NAME gui_reconnect COMMAND test_gui_reconnect)
