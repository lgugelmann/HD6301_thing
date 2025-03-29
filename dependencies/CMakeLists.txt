add_subdirectory(googletest)

# Abseil
set(ABSL_PROPAGATE_CXX_STD ON)
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
target_include_directories(imgui PUBLIC imgui imgui/backends ${SDL2_INCLUDE_DIRS})
set_source_files_properties(${IMGUI_SOURCES} PROPERTIES SKIP_LINTING ON)

# Nuked-OPL3
set(NUKED_OPL3_SOURCES
    Nuked-OPL3/opl3.c
)
add_library(nuked-opl3 STATIC ${NUKED_OPL3_SOURCES})
target_include_directories(nuked-opl3 PUBLIC .)
set_source_files_properties(${NUKED_OPL3_SOURCES} PROPERTIES SKIP_LINTING ON)
set_source_files_properties(${NUKED_OPL3_SOURCES} PROPERTIES COMPILE_FLAGS -w)
