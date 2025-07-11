set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${LLVM_BUILD_DIR}")

include(LLVMConfig)
include(AddLLVM)
add_definitions(${LLVM_DEFINITIONS})

add_subdirectory(cfg-edge)
add_subdirectory(func-call)
add_subdirectory(func-entry)
add_subdirectory(null-malloc)

