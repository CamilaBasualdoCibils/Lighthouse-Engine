cmake_minimum_required(VERSION 3.14)
project(Lighthouse VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(LIGHTHOUSE_ENABLE_TRACY "Enable Tracy profiling instead of mimalloc" OFF)

add_subdirectory(extern/OpenPeripherals)
add_subdirectory(extern/VISR)