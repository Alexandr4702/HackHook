add_executable(myhook_tests main.cpp)

target_include_directories(myhook_tests PRIVATE ${CMAKE_SOURCE_DIR})

find_package(GTest REQUIRED)
add_custom_command(TARGET myhook_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:myhook>
        $<TARGET_FILE_DIR:myhook_tests>
)
target_compile_definitions(myhook PRIVATE MYHOOK_BUILD)

target_link_libraries(myhook_tests PRIVATE myhook GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(myhook_tests)
