add_executable(gui_tests main.cpp)

target_include_directories(gui_tests PRIVATE ${CMAKE_SOURCE_DIR})

find_package(GTest REQUIRED)

target_link_libraries(gui_tests PRIVATE gui_core GTest::gtest_main)

target_compile_options(gui_tests PRIVATE -g -O0)

add_custom_command(TARGET gui_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:myhook>
        $<TARGET_FILE_DIR:gui_tests>
)

include(GoogleTest)
gtest_discover_tests(gui_tests)
