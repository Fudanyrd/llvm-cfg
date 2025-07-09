set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -fsanitize=undefined")

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_definitions(-DCFG_EDGE_PASS="${CMAKE_CURRENT_BINARY_DIR}/../pass/cfg-edge.so")
add_definitions(-DFUNC_CALL_PASS="${CMAKE_CURRENT_BINARY_DIR}/../pass/func-call.so")
add_definitions(-DFUNC_ENTRY_PASS="${CMAKE_CURRENT_BINARY_DIR}/../pass/func-entry.so")

add_library(wrapper STATIC 
    wrapper.cpp
    buf.cpp)

add_executable(cc cc.cpp)
target_link_libraries(cc wrapper)

add_definitions(-DLINK_USING_CXX)
add_executable(cxx cxx.cpp)
target_link_libraries(cxx wrapper)
