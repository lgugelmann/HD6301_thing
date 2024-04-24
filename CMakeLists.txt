cmake_minimum_required(VERSION 3.12)
project(EightBitThings)

# Abseil
set(ABSL_PROPAGATE_CXX_STD ON)
add_subdirectory(emulator/abseil-cpp)

add_subdirectory(disassembler)
add_subdirectory(emulator)
