cmake_minimum_required(VERSION 3.12)
project(EightBitThings)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Abseil
set(ABSL_PROPAGATE_CXX_STD ON)
add_subdirectory(emulator/abseil-cpp)

add_subdirectory(dependencies)

enable_testing()

add_subdirectory(disassembler)
add_subdirectory(emulator)
