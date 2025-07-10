
# manually build objects and link
set(CFG_CC_COMPILER ${CMAKE_CURRENT_BINARY_DIR}/../../wrapper/cc)
set(CFG_CXX_COMPILER ${CMAKE_CURRENT_BINARY_DIR}/../../wrapper/cxx)
set(CFG_DUMP ${CMAKE_CURRENT_BINARY_DIR}/../../tools/cfgdump)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/main.o
    COMMAND ${CFG_CXX_COMPILER} -g -fsanitize=address -c ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/main.o
    DEPENDS ${CFG_CXX_COMPILER}
)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/factorial.o
    COMMAND ${CFG_CC_COMPILER} -g -fsanitize=address -c ${CMAKE_CURRENT_SOURCE_DIR}/factorial.c -o ${CMAKE_CURRENT_BINARY_DIR}/factorial.o
    DEPENDS ${CFG_CC_COMPILER}
)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/factorial
    COMMAND ${CFG_CXX_COMPILER} -g -fsanitize=address ${CMAKE_CURRENT_BINARY_DIR}/main.o ${CMAKE_CURRENT_BINARY_DIR}/factorial.o -o ${CMAKE_CURRENT_BINARY_DIR}/factorial
    DEPENDS ${CFG_CXX_COMPILER} ${CMAKE_CURRENT_BINARY_DIR}/main.o ${CMAKE_CURRENT_BINARY_DIR}/factorial.o
)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/factorial.cfg
    COMMAND ${CFG_DUMP} ${CMAKE_CURRENT_BINARY_DIR}/factorial > ${CMAKE_CURRENT_BINARY_DIR}/factorial.cfg
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS ${CFG_DUMP} ${CMAKE_CURRENT_BINARY_DIR}/factorial
)

add_custom_target(factorial ALL
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/factorial.cfg
)
