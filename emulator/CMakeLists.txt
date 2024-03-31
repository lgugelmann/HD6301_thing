cmake_minimum_required(VERSION 3.10)
project(emulator)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(SDL2 REQUIRED)
find_package(SDL2_ttf REQUIRED)
find_package(absl REQUIRED)

add_executable(${PROJECT_NAME}
    address_space.cc
    cpu6301.cc
    emulator.cc
    graphics.cc
    ioport.cc
    ps2_keyboard.cc
)

target_link_libraries(${PROJECT_NAME}
    SDL2::SDL2
    SDL2_ttf::SDL2_ttf
    absl::flags_parse
    absl::log
    absl::log_initialize
    absl::log_flags
)

target_include_directories(${PROJECT_NAME} PUBLIC "${PROJECT_BINARY_DIR}")