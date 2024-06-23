cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(pico_graphics C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(PICO_COPY_TO_RAM 1)

pico_sdk_init()

add_subdirectory(../dependencies/PicoDVI/software/libdvi libdvi)

# These files are not part of the PicoDVI library, but of example code in that
# same repository. This library lets us use them in this project easily.
add_library(tmds_encode_font_2bpp
  ../dependencies/PicoDVI/software/apps/colour_terminal/tmds_encode_font_2bpp.S
)
target_include_directories(tmds_encode_font_2bpp PUBLIC
  ../dependencies/PicoDVI/software/apps/colour_terminal
)
target_link_libraries(tmds_encode_font_2bpp
  pico_stdlib
)

# This needs to be after the pico sdk init to get a build that uses Pico-only
# attributes to place code in RAM.
add_subdirectory(../graphics_state graphics_state)

add_compile_options(-Wall)

# ----------------------------------------

add_executable(pico_graphics
  pico_graphics.cpp
)

pico_enable_stdio_usb(pico_graphics 1)
pico_enable_stdio_uart(pico_graphics 0)

target_compile_definitions(pico_graphics PRIVATE
  DVI_VERTICAL_REPEAT=1
  DVI_N_TMDS_BUFFERS=3
  DVI_MONOCHROME_TMDS=0
  # Reduce core 1 stack size to make scratch_x large enough for the TMDS
  # encoding tables.
  PICO_CORE1_STACK_SIZE=0x200
)

target_link_libraries(pico_graphics
  pico_bit_ops
  pico_stdlib
  pico_multicore
  pico_util
  hardware_dma
  libdvi
  tmds_encode_font_2bpp
  graphics_state_lib
)

pico_add_extra_outputs(pico_graphics)
