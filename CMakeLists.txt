cmake_minimum_required(VERSION 3.16)
project(sky-blaster VERSION 0.10.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# GLES2 everywhere it exists; GL2 for desktop-GL-only stacks.
set(SKYBLASTER_GL "GLES2" CACHE STRING "GL flavour: GLES2 or GL2")
set_property(CACHE SKYBLASTER_GL PROPERTY STRINGS GLES2 GL2)
set(SKYBLASTER_DATADIR "${CMAKE_INSTALL_PREFIX}/share/sky-blaster" CACHE PATH "Installed data directory")

find_package(SDL3 REQUIRED CONFIG)

add_executable(sky-blaster
  src/main.cpp
  src/canvas.cpp
  src/renderer.cpp
  src/prims.cpp
  src/fontstash.c
  src/audio.cpp
  src/input.cpp
  src/game/data.cpp
  src/game/progress.cpp
  src/game/rules.cpp
  src/game/weapons.cpp
  src/game/enemies.cpp
  src/game/hazards.cpp
  src/game/combat.cpp
  src/game/systems.cpp
  src/game/engine.cpp
  src/game/render_scene.cpp
  src/game/render_world.cpp
  src/game/render_ui.cpp
)

target_include_directories(sky-blaster PRIVATE
  src
  third_party/fontstash
  third_party/stb
)
target_compile_definitions(sky-blaster PRIVATE SKYBLASTER_DATADIR="${SKYBLASTER_DATADIR}")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(sky-blaster PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wno-missing-field-initializers>)
  # vendored C code is left as upstream wrote it
  set_source_files_properties(src/fontstash.c PROPERTIES COMPILE_OPTIONS "-w")
endif()

if(SKYBLASTER_GL STREQUAL "GL2")
  find_package(OpenGL REQUIRED)
  target_compile_definitions(sky-blaster PRIVATE SKYBLASTER_GL2=1)
  target_link_libraries(sky-blaster PRIVATE OpenGL::GL)
else()
  find_library(GLESV2_LIBRARY NAMES GLESv2 REQUIRED)
  target_link_libraries(sky-blaster PRIVATE ${GLESV2_LIBRARY})
  # no system GLES2 headers (a desktop dev box): use the copies SDL ships
  include(CheckIncludeFile)
  check_include_file(GLES2/gl2.h HAVE_GLES2_HEADERS)
  if(NOT HAVE_GLES2_HEADERS)
    target_compile_definitions(sky-blaster PRIVATE SDL_USE_BUILTIN_OPENGL_DEFINITIONS=1)
  endif()
endif()

target_link_libraries(sky-blaster PRIVATE SDL3::SDL3 m)

install(TARGETS sky-blaster RUNTIME DESTINATION bin)
install(DIRECTORY data/ DESTINATION share/sky-blaster)
install(FILES LICENSE DESTINATION share/sky-blaster)
