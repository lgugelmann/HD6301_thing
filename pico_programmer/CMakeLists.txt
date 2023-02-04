cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(pico_programmer C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(pico_programmer
  pico_programmer.cpp
  usb_descriptors.c
)

target_include_directories(pico_programmer PUBLIC
  # make sure tinyusb can find tusb_config.h
  ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(pico_programmer PUBLIC
  pico_stdlib
  tinyusb_device
)

pico_enable_stdio_usb(pico_programmer 1)
pico_enable_stdio_uart(pico_programmer 0)

pico_add_extra_outputs(pico_programmer)
