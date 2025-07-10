cmake_minimum_required(VERSION 3.10)

project(llvm-cfg VERSION 0.1 LANGUAGES C CXX)

if(NOT EXISTS ${LLVM_BUILD_DIR})
    message(FATAL_ERROR "LLVM_BUILD_DIR is not set or does not exist. Please set it to the path of your LLVM build directory.")
endif()

add_subdirectory(tools)
add_subdirectory(pass)
add_subdirectory(wrapper)
add_subdirectory(demo)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
