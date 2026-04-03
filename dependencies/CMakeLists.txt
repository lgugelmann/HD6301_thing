add_subdirectory(googletest)

# Google Benchmark
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
add_subdirectory(benchmark)

# Abseil
set(ABSL_PROPAGATE_CXX_STD ON)
# Certain Abseil headers don't pass e.g. nullability warnings for Clang-20+,
# which causes errors with -Werror. The setting below marks them as system
# headers, which suppresses the warnings.
set(ABSL_USE_SYSTEM_INCLUDES ON)
add_subdirectory(abseil-cpp)

# Dear ImGui
find_package(SDL3 REQUIRED)
set(IMGUI_SOURCES
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_widgets.cpp
    imgui/imgui_tables.cpp
    imgui/backends/imgui_impl_sdl3.cpp
    imgui/backends/imgui_impl_sdlrenderer3.cpp
)
add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC imgui imgui/backends)
target_link_libraries(imgui PUBLIC SDL3::SDL3)
set_source_files_properties(${IMGUI_SOURCES} PROPERTIES SKIP_LINTING ON)

# Nuked-OPL3
set(NUKED_OPL3_SOURCES
    Nuked-OPL3/opl3.c
)
add_library(nuked-opl3 STATIC ${NUKED_OPL3_SOURCES})
target_include_directories(nuked-opl3 PUBLIC .)
set_source_files_properties(${NUKED_OPL3_SOURCES} PROPERTIES SKIP_LINTING ON)
set_source_files_properties(${NUKED_OPL3_SOURCES} PROPERTIES COMPILE_FLAGS -w)

# ASL assembler (built from submodule)
set(ASL_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/asl)
set(ASL_BIN_DIR ${CMAKE_CURRENT_BINARY_DIR}/asl-bin)

# Write Makefile.def into the submodule (the build system requires it, but
# intentionally does not ship one — users are expected to create it).
file(WRITE ${ASL_SOURCE_DIR}/Makefile.def
"CC = gcc
CFLAGS = -O3
OBJDIR =
HOST_OBJEXTENSION = .o
LD = \$(CC)
LDFLAGS =
HOST_EXEXTENSION =

TARG_OBJDIR = \$(OBJDIR)
TARG_CC = \$(CC)
TARG_CFLAGS = \$(CFLAGS)
TARG_OBJEXTENSION = \$(HOST_OBJEXTENSION)
TARG_LD = \$(LD)
TARG_LDFLAGS = \$(LDFLAGS)
TARG_EXEXTENSION = \$(HOST_EXEXTENSION)

INSTROOT := /usr/local
")

# Build only the 'binaries' target to avoid the LaTeX docs dependency that
# 'make all' and 'make install' pull in. Using add_custom_command so that
# the output files are declared as proper Ninja edges (ExternalProject's
# BUILD_BYPRODUCTS doesn't create reliable cross-directory ordering).
add_custom_command(
    OUTPUT ${ASL_BIN_DIR}/asl ${ASL_BIN_DIR}/p2bin
    COMMAND make binaries
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ASL_BIN_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy ${ASL_SOURCE_DIR}/asl ${ASL_BIN_DIR}/asl
    COMMAND ${CMAKE_COMMAND} -E copy ${ASL_SOURCE_DIR}/p2bin ${ASL_BIN_DIR}/p2bin
    WORKING_DIRECTORY ${ASL_SOURCE_DIR}
    COMMENT "Building ASL assembler"
)
add_custom_target(asl_build DEPENDS ${ASL_BIN_DIR}/asl ${ASL_BIN_DIR}/p2bin)

set(ASL_EXECUTABLE ${ASL_BIN_DIR}/asl CACHE INTERNAL "Path to asl assembler")
set(P2BIN_EXECUTABLE ${ASL_BIN_DIR}/p2bin CACHE INTERNAL "Path to p2bin")
