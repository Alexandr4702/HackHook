add_executable(dataParser dataParser.cpp)

set(TBB_DIR "${MINGW_PREFIX}/lib/cmake/TBB")

find_package(TBB REQUIRED COMPONENTS tbb)

if(TBB_FOUND)
    message(STATUS "TBB found, version: ${TBB_VERSION}")
    message(STATUS "TBB libraries: ${TBB_LIBRARIES}")
    message(STATUS "TBB includes: ${TBB_INCLUDE_DIRS}")
else()
    message(FATAL_ERROR "TBB not found! Install with: pacman -S mingw-w64-x86_64-tbb")
endif()

if(TARGET TBB::tbb)
    target_link_libraries(dataParser PRIVATE TBB::tbb)
else()
    target_include_directories(dataParser PRIVATE ${TBB_INCLUDE_DIRS})
    target_link_libraries(dataParser PRIVATE ${TBB_LIBRARIES})
endif()