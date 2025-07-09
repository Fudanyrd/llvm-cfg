set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/..)

add_executable(cfgdump cfgdump.cc)
add_executable(secdump secdump.c)
