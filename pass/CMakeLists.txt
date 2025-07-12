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

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/build.sh
  COMMAND CC=${CMAKE_C_COMPILER} CXX=${CMAKE_CXX_COMPILER} ${CMAKE_CURRENT_SOURCE_DIR}/gen.sh ${CMAKE_CURRENT_BINARY_DIR}/build.sh
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gen.sh
)

add_custom_target(script ALL 
  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/build.sh
)
