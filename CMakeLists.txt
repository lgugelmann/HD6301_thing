cmake_minimum_required(VERSION 3.12)
project(EightBitThings)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

option(TSAN_BUILD "Set up a TSan build" OFF)

if (TSAN_BUILD)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread -fno-omit-frame-pointer)
endif()

add_subdirectory(dependencies)

enable_testing()

add_subdirectory(disassembler)
add_subdirectory(emulator)
