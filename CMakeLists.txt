# Cross-platform build for the portable RFHUtil core and its tests.
# The Windows MFC application keeps building with RFHUtil.sln.
cmake_minimum_required(VERSION 3.21)

project(rfhutil LANGUAGES CXX)

if(WIN32)
    message(FATAL_ERROR "The CMake build is Linux-only for now; on Windows use RFHUtil.sln")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# compile_commands.json in the build directory, for clangd and other tools.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(RFHUTIL_BUILD_TESTS "Build the core tests" ON)
option(RFHUTIL_FETCH_GTEST "Download GoogleTest when no installed package is found" OFF)

set(RFHUTIL_DEFAULT_MQ_HOME "/opt/mqm")
set(MQ_HOME "${RFHUTIL_DEFAULT_MQ_HOME}" CACHE PATH "IBM MQ installation directory (headers only; the library is loaded at run time)")

add_subdirectory(core)

if(RFHUTIL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
