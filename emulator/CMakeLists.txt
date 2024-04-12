cmake_minimum_required(VERSION 3.10)
project(emulator)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_CLANG_TIDY "clang-tidy;-checks=abseil-*,modernize-*,-modernize-use-trailing-return-type,-modernize-use-bool-literals,performance-*,readability-*,-readability-identifier-length,-readability-function-cognitive-complexity,-readability-magic-numbers,-readability-implicit-bool-conversion")

find_package(SDL2 REQUIRED)
find_package(absl REQUIRED)

add_executable(${PROJECT_NAME}
    address_space.cc
    cpu6301.cc
    emulator.cc
    graphics.cc
    hexdump.cc
    ioport.cc
    ps2_keyboard.cc
    ram.cc
    rom.cc
    hd6301_serial.cc
    sound_opl3.cc
    timer.cc
    Nuked-OPL3/opl3.c
)

target_link_libraries(${PROJECT_NAME}
    SDL2::SDL2
    absl::flags_parse
    absl::log
    absl::log_initialize
    absl::log_flags
    absl::status
    absl::statusor
    absl::synchronization
)

target_include_directories(${PROJECT_NAME} PUBLIC "${PROJECT_BINARY_DIR}")
target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(graphics_tester
    graphics_tester.cc
    address_space.cc
    graphics.cc
    hexdump.cc
)

target_link_libraries(graphics_tester
    SDL2::SDL2
    absl::log
 )