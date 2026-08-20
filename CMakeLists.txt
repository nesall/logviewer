# =============================================================================
#  1. Project Configuration
# =============================================================================
cmake_minimum_required(VERSION 3.16)
project(logviewer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# =============================================================================
#  2. Fetch Dependencies
# =============================================================================
include(FetchContent)

# Docking branch (not a tagged release) -- needed for dockable panel layout
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        docking
)

FetchContent_Declare(
  implot
  GIT_REPOSITORY https://github.com/epezent/implot.git
  GIT_TAG        v1.0
)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG        3.4
)

FetchContent_MakeAvailable(imgui implot glfw)

# =============================================================================
#  3. Create ImGui Library Target
# =============================================================================
# ImGui doesn't provide a CMake target, so create one.
# Core files live at the repo root; backends live under backends/.
add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(imgui PUBLIC glfw)

# =============================================================================
#  4. Create ImPlot Library Target
# =============================================================================
# ImPlot also doesn't ship a CMakeLists; build it against the same imgui.
add_library(implot STATIC
  ${implot_SOURCE_DIR}/implot.cpp
  ${implot_SOURCE_DIR}/implot_items.cpp
)

target_include_directories(implot PUBLIC
  ${implot_SOURCE_DIR}
)

target_link_libraries(implot PUBLIC imgui)

# Note: portable-file-dialogs ships its own CMakeLists.txt that already
# defines an INTERFACE target named "portable_file_dialogs" -- no need to
# create one ourselves; FetchContent_MakeAvailable() above already did it.

# =============================================================================
#  5. Executable & Sources
# =============================================================================
add_executable(logviewer
  include/3rdparty/zstd/zstd.c
  app.rc
  src/main.cpp
  src/utils/utils.h
  src/utils/utils.cpp
  src/core/logsession.h
  src/core/logsession.cpp
  src/core/channel.h
  src/core/channel.cpp
  src/core/table2d.h
  src/core/table2d.cpp
  src/core/regime.h
  src/core/regime.cpp
  src/core/curve1d.h
  src/core/curve1d.cpp
  src/engine/decimator.h
  src/engine/decimator.cpp
  src/engine/ve_analyzer.h
  src/engine/ve_analyzer.cpp
  src/engine/regime_analyzer.h
  src/engine/regime_analyzer.cpp
  src/engine/virtualdyno.h
  src/engine/virtualdyno.cpp
  src/io/logparser.h
  src/io/megasquirtcsvparser.h
  src/io/megasquirtcsvparser.cpp
  src/io/haltechdlparser.h
  src/io/haltechdlparser.cpp
  src/io/plogparser.h
  src/io/plogparser.cpp
  src/io/csvexporter.h
  src/io/csvexporter.cpp
  src/ui/app.h
  src/ui/app.cpp
  src/ui/plotpanel.h
  src/ui/plotpanel.cpp
  src/ui/timeseriespanel.h
  src/ui/timeseriespanel.cpp
  src/ui/scatterpanel.h
  src/ui/scatterpanel.cpp
  src/ui/statuspanel.h
  src/ui/statuspanel.cpp
  src/ui/tableeditorpanel.h
  src/ui/tableeditorpanel.cpp
  src/ui/veanalysispanel.h
  src/ui/veanalysispanel.cpp
  src/ui/tableoverlaypanel.h
  src/ui/tableoverlaypanel.cpp
  src/ui/driveregimepanel.h
  src/ui/driveregimepanel.cpp
  src/ui/curve2dpanel.h
  src/ui/curve2dpanel.cpp
  src/ui/virtualdynopanel.h
  src/ui/virtualdynopanel.cpp
  src/ui/ui_helpers.h
  src/core/formula_evaluator.h
  src/core/formula_evaluator.cpp
)

add_custom_command(TARGET logviewer POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    "${CMAKE_BINARY_DIR}/assets"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    "$<TARGET_FILE_DIR:logviewer>/assets"
  COMMENT "Copying assets to both top-level build directory and target output folder"
)

# =============================================================================
#  6. Includes & Linking
# =============================================================================
target_include_directories(logviewer PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include
)

find_package(OpenGL REQUIRED)

target_link_libraries(logviewer PRIVATE
  imgui
  implot
  glfw
  
  OpenGL::GL
)


if (MSVC)
  target_compile_options(logviewer PRIVATE /bigobj)
endif()
