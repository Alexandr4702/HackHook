find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

include_directories(.)

add_executable(gui_app
    main.cpp
    mainwindow.cpp
    mainwindow.h
    WindowSelectorCombo.h
    mainwindow.ui
)

target_link_libraries(gui_app
    PRIVATE Qt6::Widgets injector common
)

add_dependencies(gui_app generate_flatbuffers_code)
