cmake_minimum_required(VERSION 3.14)
project(Lighthouse VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(LIGHTHOUSE_ENABLE_TRACY "Enable Tracy profiling instead of mimalloc" OFF)
include(CTest)

add_subdirectory(extern/OpenPeripherals)
add_subdirectory(extern/VISR)
add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(Tests)
endif()

option(LIGHTHOUSE_BUILD_EXAMPLES "Build Lighthouse examples" OFF)
if(LIGHTHOUSE_BUILD_EXAMPLES)
    add_subdirectory(Examples)
endif()


include(cmake/GetDeps.cmake)
