set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(graphics_state_SOURCES
  graphics_state.cc
)

add_library(graphics_state_lib STATIC ${graphics_state_SOURCES})
target_include_directories(graphics_state_lib PUBLIC .)
target_compile_options(graphics_state_lib PRIVATE -Wall -Wextra -Werror)
if (PICO_COPY_TO_RAM)
  add_compile_definitions(GRAPHICS_COPY_TO_RAM)
  target_link_libraries(graphics_state_lib
    pico_stdlib
  )
endif()

# Only build the tests is we're in a context where GTest is already available
# and skip them e.g. in pico builds.
if (TARGET GTest::gtest_main)
  enable_testing()
  set(test_SOURCES
    graphics_state_test.cc
  )
  add_executable(graphics_state_test ${test_SOURCES})
  set(TESTDATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/testdata")
  add_definitions(-DTESTDATA_DIR="${TESTDATA_DIR}")
  target_compile_options(graphics_state_test PRIVATE -Wall -Wextra -Werror -Wno-gcc-compat)
  target_link_libraries(graphics_state_test
      GTest::gtest_main
      GTest::gmock_main
      graphics_state_lib
  )
  gtest_discover_tests(graphics_state_test)
endif()
