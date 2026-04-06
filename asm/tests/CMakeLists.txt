# Unit tests for the HD6301 assembly code.
#
# These tests load the assembled ROM binary and symbol table, then execute
# assembly subroutines inside an emulated CPU to verify their behaviour.
# The ROM must be built first (ninja rom); the test target declares this
# dependency so CMake/ctest handles the ordering automatically.

set(ROM_BIN     "${CMAKE_BINARY_DIR}/asm/rom.bin")
set(SYMBOLS_TXT "${CMAKE_BINARY_DIR}/asm/symbols.txt")

add_executable(asm_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/fat16_test.cc
    ${CMAKE_SOURCE_DIR}/emulator/address_space.cc
    ${CMAKE_SOURCE_DIR}/emulator/cpu6301.cc
    ${CMAKE_SOURCE_DIR}/emulator/hd6301_serial.cc
    ${CMAKE_SOURCE_DIR}/emulator/ioport.cc
    ${CMAKE_SOURCE_DIR}/emulator/timer.cc
)

target_include_directories(asm_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/emulator
)

target_compile_definitions(asm_tests PRIVATE
    ROM_PATH="${ROM_BIN}"
    SYMBOLS_PATH="${SYMBOLS_TXT}"
)

target_compile_options(asm_tests PRIVATE -Wall -Wextra -Werror)

target_link_libraries(asm_tests
    GTest::gtest_main
    GTest::gmock
    absl::strings
    absl::flags_parse
    absl::log
    absl::log_initialize
    absl::log_flags
    absl::status
    absl::statusor
    absl::synchronization
)

# The tests require the ROM binary and symbol table to be assembled first.
add_dependencies(asm_tests rom)

gtest_discover_tests(asm_tests)
