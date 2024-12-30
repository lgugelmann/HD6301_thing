project(asm)

# Make sure we have ASL and the dependency generation tool available
find_program(ASL asl REQUIRED)
find_program(P2BIN p2bin REQUIRED)
find_program(DEPGEN ${PROJECT_SOURCE_DIR}/depgen.py REQUIRED)
find_program(PROGRAMMER ${CMAKE_SOURCE_DIR}/pico_programmer/programmer.py)

#
# Commands to assemble the monitor
#
add_custom_command(
    OUTPUT ${PROJECT_BINARY_DIR}/monitor.d
    COMMAND ${DEPGEN} ${PROJECT_SOURCE_DIR}/monitor.s > ${PROJECT_BINARY_DIR}/monitor.d
    DEPENDS ${PROJECT_SOURCE_DIR}/monitor.s
)
add_custom_command(
    OUTPUT ${PROJECT_BINARY_DIR}/monitor.bin
           ${PROJECT_BINARY_DIR}/monitor.p
           ${PROJECT_SOURCE_DIR}/monitor.inc
           ${PROJECT_SOURCE_DIR}/monitor.lst
    COMMAND ${ASL} -Werror -q -U -a -L -o ${PROJECT_BINARY_DIR}/monitor.p ${PROJECT_SOURCE_DIR}/monitor.s
    COMMAND ${P2BIN} ${PROJECT_BINARY_DIR}/monitor.p ${PROJECT_BINARY_DIR}/monitor.bin
    DEPENDS ${PROJECT_SOURCE_DIR}/monitor.s
            ${PROJECT_BINARY_DIR}/monitor.d
    DEPFILE monitor.d
    COMMENT "Assembling monitor"
)

#
# Commands to assemble the programs
#
add_custom_command(
    OUTPUT ${PROJECT_BINARY_DIR}/programs.d
    COMMAND ${DEPGEN} ${PROJECT_SOURCE_DIR}/programs.s > ${PROJECT_BINARY_DIR}/programs.d
    DEPENDS ${PROJECT_SOURCE_DIR}/programs.s
            ${PROJECT_SOURCE_DIR}/monitor.inc
)
add_custom_command(
    OUTPUT ${PROJECT_BINARY_DIR}/programs.bin
           ${PROJECT_BINARY_DIR}/programs.p
           ${PROJECT_SOURCE_DIR}/programs.lst
    COMMAND ${ASL} -Werror -q -U -L -o ${PROJECT_BINARY_DIR}/programs.p ${PROJECT_SOURCE_DIR}/programs.s
    COMMAND ${P2BIN} ${PROJECT_BINARY_DIR}/programs.p ${PROJECT_BINARY_DIR}/programs.bin
    DEPENDS ${PROJECT_SOURCE_DIR}/programs.s
            ${PROJECT_SOURCE_DIR}/monitor.inc
            ${PROJECT_BINARY_DIR}/programs.d
    DEPFILE programs.d
    COMMENT "Assembling programs"
)

#
# Programs for assembling the full ROM
#
add_custom_command(
    OUTPUT ${PROJECT_BINARY_DIR}/rom.bin
    COMMAND ${P2BIN} -segment CODE ${PROJECT_BINARY_DIR}/monitor.p ${PROJECT_BINARY_DIR}/programs.p ${PROJECT_BINARY_DIR}/rom.bin
    DEPENDS ${PROJECT_BINARY_DIR}/monitor.p
            ${PROJECT_BINARY_DIR}/programs.p
    COMMENT "Generating ROM"
)

#
# Targets for monitor, programs, and rom
#
foreach(TARGET monitor programs rom)
    add_custom_target(${TARGET} ALL DEPENDS ${PROJECT_BINARY_DIR}/${TARGET}.bin)
endforeach()

#
# Programming targets
#
add_custom_target(monitor_prog
    COMMAND ${PROGRAMMER} write_flash ${PROJECT_BINARY_DIR}/monitor.bin
    DEPENDS ${PROJECT_BINARY_DIR}/monitor.bin
    COMMENT "Programming monitor.bin"
)
add_custom_target(programs_prog
    COMMAND ${PROGRAMMER} write_flash --address 0x8000 ${PROJECT_BINARY_DIR}/programs.bin
    DEPENDS ${PROJECT_BINARY_DIR}/programs.bin
    COMMENT "Programming programs.bin"
)
add_custom_target(prog
    COMMAND ${PROGRAMMER} write_flash ${PROJECT_BINARY_DIR}/rom.bin
    DEPENDS ${PROJECT_BINARY_DIR}/rom.bin
    COMMENT "Programming rom.bin"
)
