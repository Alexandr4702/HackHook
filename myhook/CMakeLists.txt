
add_library(myhook SHARED 
    MyHook.cpp 
    IpcHandler.cpp
    MemDumper.cpp
    MemoryScanner.cpp
    BMH_SIMD.cpp
)

include_directories(.)

target_link_libraries(myhook PRIVATE common)
target_compile_options(myhook PRIVATE -mavx2 -O3)

add_dependencies(myhook generate_flatbuffers_code)

set_target_properties(myhook PROPERTIES PREFIX "" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
target_compile_definitions(myhook PRIVATE -DWIN32_LEAN_AND_MEAN)

add_subdirectory(Tests)