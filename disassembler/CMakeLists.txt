cmake_minimum_required(VERSION 3.12)
project(disassembler)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Disassembler
set(disassembler_SOURCES
  disassembler.cc
  main.cc
)

add_executable(disassembler ${disassembler_SOURCES})

target_compile_options(disassembler PRIVATE -Wall -Wextra -Werror -Wno-gcc-compat)

target_link_libraries(disassembler
    absl::flags_parse
    absl::log
    absl::log_initialize
    absl::log_flags
    absl::status
    absl::statusor
)

add_library(disassembler_lib STATIC
  disassembler.cc
)
target_link_libraries(disassembler_lib
    absl::log
    absl::log_initialize
    absl::log_flags
    absl::status
    absl::statusor
)

# Tests
enable_testing()
include(GoogleTest)

set(test_SOURCES
  disassembler_test.cc
)

add_executable(disassembler_test ${test_SOURCES})
set(TESTDATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/testdata")
add_definitions(-DTESTDATA_DIR="${TESTDATA_DIR}")
target_compile_options(disassembler_test PRIVATE -Wall -Wextra -Werror -Wno-gcc-compat)
target_link_libraries(disassembler_test
    GTest::gtest_main
    GTest::gmock_main
    disassembler_lib
)
gtest_discover_tests(disassembler_test)
