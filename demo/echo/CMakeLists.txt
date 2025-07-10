
# manually build objects and link
set(CFG_CC_COMPILER ${CMAKE_CURRENT_BINARY_DIR}/../../wrapper/cc)
set(CFG_DUMP ${CMAKE_CURRENT_BINARY_DIR}/../../tools/cfgdump)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/main.o
    COMMAND ${CFG_CC_COMPILER} -g -fsanitize=address -c ${CMAKE_CURRENT_SOURCE_DIR}/main.c -o ${CMAKE_CURRENT_BINARY_DIR}/main.o
    DEPENDS ${CFG_CC_COMPILER}
)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/puts.o
    COMMAND ${CFG_CC_COMPILER} -g -fsanitize=address -c ${CMAKE_CURRENT_SOURCE_DIR}/puts.c -o ${CMAKE_CURRENT_BINARY_DIR}/puts.o
    DEPENDS ${CFG_CC_COMPILER}
)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/echo
    COMMAND ${CFG_CC_COMPILER} -g -fsanitize=address ${CMAKE_CURRENT_BINARY_DIR}/main.o ${CMAKE_CURRENT_BINARY_DIR}/puts.o -o ${CMAKE_CURRENT_BINARY_DIR}/echo
    DEPENDS ${CFG_CC_COMPILER} ${CMAKE_CURRENT_BINARY_DIR}/main.o ${CMAKE_CURRENT_BINARY_DIR}/puts.o
)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/echo.cfg
    COMMAND ${CFG_DUMP} ${CMAKE_CURRENT_BINARY_DIR}/echo > ${CMAKE_CURRENT_BINARY_DIR}/echo.cfg
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS ${CFG_DUMP} ${CMAKE_CURRENT_BINARY_DIR}/echo
)

add_custom_target(echo ALL
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/echo.cfg
)
