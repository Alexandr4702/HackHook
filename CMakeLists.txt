cmake_minimum_required(VERSION 3.10)

project(SetWindowsHookExInjection)

# set(CMAKE_GENERATOR_PLATFORM x64)
set(CMAKE_CXX_STANDARD 23)

find_package(Boost CONFIG REQUIRED)

add_library(common STATIC common/utility.cpp)

# Injector
add_library(injector STATIC injector/injector.cpp)
# DLL (renamed file)
add_library(myhook SHARED 
myhook/MyHook.cpp 
myhook/IpcHandler.cpp
myhook/MemDumper.cpp
)

target_link_libraries(injector PRIVATE common)
target_link_libraries(myhook PRIVATE common)

include_directories(.)
include_directories(flatbuffers/include)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/generated_flatbuffers)

add_subdirectory(flatbuffers)
add_subdirectory(gui)
add_subdirectory(dataParser)

add_custom_target(generate_flatbuffers_code ALL
  COMMAND flatc --cpp -o ${CMAKE_CURRENT_BINARY_DIR}/generated_flatbuffers ${CMAKE_CURRENT_SOURCE_DIR}/interface.fbs
  BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/generated_flatbuffers/interface_generated.cpp ${CMAKE_CURRENT_BINARY_DIR}/generated_flatbuffers/interface_generated.h
  VERBATIM
)

add_dependencies(injector generate_flatbuffers_code myhook)
add_dependencies(myhook generate_flatbuffers_code)
add_dependencies(common generate_flatbuffers_code)

set_target_properties(myhook PROPERTIES PREFIX "")
target_compile_definitions(myhook PRIVATE -DWIN32_LEAN_AND_MEAN)

set_target_properties(myhook PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set_target_properties(injector PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
