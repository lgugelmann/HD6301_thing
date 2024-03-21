cmake_minimum_required(VERSION 3.10)
project(emulator)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(SDL2 REQUIRED)

add_executable(emulator emulator.cc cpu6301.cc address_space.cc)

target_link_libraries(emulator ${SDL2_LIBRARIES})

target_include_directories(emulator PUBLIC "${PROJECT_BINARY_DIR}")