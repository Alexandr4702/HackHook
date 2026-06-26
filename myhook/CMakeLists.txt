
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

if(MINGW)
    get_filename_component(MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    add_custom_command(TARGET myhook POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MINGW_BIN_DIR}/libgcc_s_seh-1.dll"
            "${MINGW_BIN_DIR}/libstdc++-6.dll"
            "${MINGW_BIN_DIR}/libwinpthread-1.dll"
            "$<TARGET_FILE_DIR:myhook>"
        VERBATIM
    )
endif()

add_subdirectory(Tests)