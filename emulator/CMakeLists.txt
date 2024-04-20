cmake_minimum_required(VERSION 3.12)
project(emulator)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

option(GPERFTOOLS_BUILD "Enable profiling with gperftools" OFF)
option(TSAN_BUILD "Set up a TSan build" OFF)

if (TSAN_BUILD)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread -fno-omit-frame-pointer)
endif()

# Abseil
set(ABSL_PROPAGATE_CXX_STD ON)
add_subdirectory(abseil-cpp)

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

# Nuked-OPL3
set(NUKED_OPL3_SOURCES
    Nuked-OPL3/opl3.c
)
add_library(nuked-opl3 STATIC ${NUKED_OPL3_SOURCES})
set_source_files_properties(${NUKED_OPL3_SOURCES} PROPERTIES SKIP_LINTING ON)
set_source_files_properties(${NUKED_OPL3_SOURCES} PROPERTIES COMPILE_FLAGS -w)

set(emulator_SOURCES
    address_space.cc
    cpu6301.cc
    emulator.cc
    graphics.cc
    hd6301_serial.cc
    hexdump.cc
    ioport.cc
    ps2_keyboard.cc
    ram.cc
    rom.cc
    sound_opl3.cc
    timer.cc
    tl16c2550.cc
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
set(CMAKE_CXX_CLANG_TIDY "clang-tidy;-checks=${EMULATOR_CLANG_TIDY_CHECKS}")

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
    nuked-opl3
)
if (RtMidi_FOUND)
    target_link_libraries(emulator RtMidi::rtmidi)
endif()
if (GPERFTOOLS_BUILD)
    target_link_libraries(emulator profiler)
endif()
