add_executable(tests_main main.cpp)

target_include_directories(tests_main PRIVATE ${CMAKE_SOURCE_DIR})

find_package(GTest REQUIRED)
target_link_libraries(tests_main PRIVATE GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(tests_main)
