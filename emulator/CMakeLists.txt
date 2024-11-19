cmake_minimum_required(VERSION 3.12)
project(emulator)

option(GPERFTOOLS_BUILD "Enable profiling with gperftools" OFF)
option(USE_CLANG_TIDY "Use clang-tidy for static analysis" OFF)

# SDL2
find_package(SDL2 REQUIRED)

# RtMidi -- Optional
find_package(RtMidi QUIET)
if (RtMidi_FOUND)
    message(STATUS "RtMidi found, adding MIDI support")
    add_compile_definitions(HAVE_MIDI)
else()
    message(STATUS "RtMidi not found, MIDI support disabled")
endif()

add_subdirectory(../graphics_state graphics_state)

set(emulator_SOURCES
    address_space.cc
    cpu6301.cc
    emulator.cc
    graphics.cc
    hd6301_serial.cc
    hd6301_thing.cc
    ioport.cc
    ps2_keyboard.cc
    ram.cc
    rom.cc
    sd_card_spi.cc
    sound_opl3.cc
    spi.cc
    timer.cc
    tl16c2550.cc
    w65c22.cc
)
if (RtMidi_FOUND)
    list(APPEND emulator_SOURCES midi_to_serial.cc)
endif()

set(EMULATOR_CLANG_TIDY_CHECKS
    abseil-*
    modernize-*
    -modernize-use-trailing-return-type
    -modernize-use-bool-literals
    performance-*
    readability-*
    -readability-identifier-length
    -readability-function-cognitive-complexity
    -readability-magic-numbers
    -readability-implicit-bool-conversion
)
list(JOIN EMULATOR_CLANG_TIDY_CHECKS "," EMULATOR_CLANG_TIDY_CHECKS)
if (USE_CLANG_TIDY)
    message(STATUS "Using clang-tidy for static analysis")
    set(CMAKE_CXX_CLANG_TIDY "clang-tidy;-checks=${EMULATOR_CLANG_TIDY_CHECKS}")
else()
    message(STATUS "Not using clang-tidy for static analysis")
endif()

add_library(hexdump_lib STATIC
    hexdump.cc
)
target_link_libraries(hexdump_lib
    absl::strings
)

add_executable(emulator ${emulator_SOURCES})

target_include_directories(emulator PUBLIC "${PROJECT_SOURCE_DIR}" ${SDL2_INCLUDE_DIRS})

target_compile_options(emulator PRIVATE -Wall -Wextra -Werror -Wno-gcc-compat)

target_link_libraries(emulator
    SDL2::SDL2
    absl::flags_parse
    absl::log
    absl::log_initialize
    absl::log_flags
    absl::status
    absl::statusor
    absl::synchronization
    imgui
    nuked-opl3
    disassembler_lib
    graphics_state_lib
    hexdump_lib
)
if (RtMidi_FOUND)
    target_link_libraries(emulator RtMidi::rtmidi)
endif()
if (GPERFTOOLS_BUILD)
    target_link_libraries(emulator profiler)
endif()

enable_testing()

include(GoogleTest)
add_executable(emulator_tests
    address_space.cc
    hexdump_test.cc
    ioport.cc
    hexdump.cc
    w65c22.cc
    w65c22_test.cc
)
target_link_libraries(emulator_tests
    GTest::gtest_main
    hexdump_lib
    absl::strings
    absl::flags_parse
    absl::log
    absl::log_initialize
    absl::log_flags
    absl::status
    absl::statusor
    absl::synchronization
)
gtest_discover_tests(emulator_tests)
