set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -fsanitize=undefined")
  add_definitions(-DCFG_PRINT_DEBUG_OUTPUT)
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_definitions(-DCFG_EDGE_PASS="${CMAKE_CURRENT_BINARY_DIR}/../pass/cfg-edge/cfg-edge.so")
add_definitions(-DFUNC_CALL_PASS="${CMAKE_CURRENT_BINARY_DIR}/../pass/func-call/func-call.so")
add_definitions(-DFUNC_ENTRY_PASS="${CMAKE_CURRENT_BINARY_DIR}/../pass/func-entry/func-entry.so")
add_definitions(-DCFG_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

add_library(wrapper STATIC 
    wrapper.cpp
    buf.cpp)

add_executable(cc cc.cpp)
target_link_libraries(cc wrapper)

add_executable(cxx cxx.cpp)
target_link_libraries(cxx wrapper)

add_library(cfgmalloc_static STATIC cfgmalloc.c)
set_target_properties(cfgmalloc_static PROPERTIES OUTPUT_NAME "cfgmalloc")

add_library(cfgmalloc_shared SHARED cfgmalloc.c)
set_target_properties(cfgmalloc_shared PROPERTIES OUTPUT_NAME "cfgmalloc")
