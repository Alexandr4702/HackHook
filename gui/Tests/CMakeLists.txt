add_executable(gui_tests main.cpp)

target_include_directories(gui_tests PRIVATE ${CMAKE_SOURCE_DIR})

find_package(GTest REQUIRED)
add_custom_command(TARGET gui_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:myhook>
        $<TARGET_FILE_DIR:gui_tests>
)

target_link_libraries(gui_tests PRIVATE GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(gui_tests)
