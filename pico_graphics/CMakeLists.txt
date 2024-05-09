cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(pico_graphics C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(PICO_COPY_TO_RAM 1)

pico_sdk_init()

add_subdirectory(../dependencies/PicoDVI/software/libdvi libdvi)

add_compile_options(-Wall)

# ----------------------------------------

add_executable(pico_graphics
  pico_graphics.cpp
  tmds_encode_font_2bpp.S
  tmds_encode_font_2bpp.h
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
)

pico_add_extra_outputs(pico_graphics)
