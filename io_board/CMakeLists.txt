project(io_board)

find_program(SD_IMAGE_GEN ${CMAKE_SOURCE_DIR}/tools/make_fat16_sd_image.sh)

add_custom_command(
  OUTPUT ${PROJECT_BINARY_DIR}/sd_card.img
  COMMAND ${SD_IMAGE_GEN} ${PROJECT_BINARY_DIR}/sd_card.img
)

add_custom_target(sd_image DEPENDS ${PROJECT_BINARY_DIR}/sd_card.img)