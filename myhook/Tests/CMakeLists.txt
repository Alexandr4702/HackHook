add_executable(tests_main main.cpp)

target_include_directories(tests_main PRIVATE ${CMAKE_SOURCE_DIR})

find_package(GTest REQUIRED)
add_custom_command(TARGET tests_main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:myhook>
        $<TARGET_FILE_DIR:tests_main>
)
target_compile_definitions(myhook PRIVATE MYHOOK_BUILD)

target_link_libraries(tests_main PRIVATE myhook GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(tests_main)
