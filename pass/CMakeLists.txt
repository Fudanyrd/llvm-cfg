set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${LLVM_BUILD_DIR}/share/llvm/cmake")

include(LLVMConfig)
include(AddLLVM)

# file(GLOB PASS_SOURCES "*.cpp")
# foreach(SOURCE ${PASS_SOURCES})
#     get_filename_component(NAME ${SOURCE} NAME_WE)
#     add_library(${NAME} SHARED ${SOURCE})
# endforeach()

add_definitions(${LLVM_DEFINITIONS})

#add_llvm_pass_plugin(cfg-edge CfgEdgePass.cpp)
#add_llvm_pass_plugin(func-call FuncCallPass.cpp)
#add_llvm_pass_plugin(func-entry FuncEntryPass.cpp)

add_subdirectory(cfg-edge)
add_subdirectory(func-call)
add_subdirectory(func-entry)

