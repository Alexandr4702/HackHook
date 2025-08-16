
add_library(myhook SHARED 
    MyHook.cpp 
    IpcHandler.cpp
    MemDumper.cpp
)

include_directories(.)

target_link_libraries(myhook PRIVATE common)

add_dependencies(myhook generate_flatbuffers_code)

set_target_properties(myhook PROPERTIES PREFIX "")
target_compile_definitions(myhook PRIVATE -DWIN32_LEAN_AND_MEAN)
