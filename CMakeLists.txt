cmake_minimum_required(VERSION 3.20)
project(Worldbuilder LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

# Single-config generators (Ninja, MinGW Makefiles): default to an optimised build.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Bake the C++ runtime into the exe so it runs on any Windows PC without extra installs.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)

# raylib: window, input, OpenGL rendering
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(raylib
  URL https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz)

# raygui: immediate-mode UI (header only, so nothing to build)
FetchContent_Declare(raygui
  URL https://github.com/raysan5/raygui/archive/refs/tags/4.0.tar.gz
  SOURCE_SUBDIR do-not-build)

FetchContent_MakeAvailable(raylib raygui)

add_executable(Worldbuilder
  src/main.cpp
  src/planet.cpp
  src/tilemap.cpp
  src/tectonics.cpp
  src/resources.cpp
  src/climate.cpp
  src/rivers.cpp
  src/persistence.cpp
  src/document.cpp
  src/names.cpp
  src/politics.cpp
  src/trade.cpp
  src/history.cpp
  src/raygui_impl.c)
target_include_directories(Worldbuilder PRIVATE src ${raygui_SOURCE_DIR}/src)
find_package(Threads REQUIRED)
target_link_libraries(Worldbuilder PRIVATE raylib Threads::Threads)

if(MSVC)
  target_compile_options(Worldbuilder PRIVATE /W3 /utf-8)
  set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT Worldbuilder)
  # Release exe opens just the app window, with no console window behind it
  target_link_options(Worldbuilder PRIVATE $<$<CONFIG:Release>:/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup>)
elseif(MINGW)
  target_link_options(Worldbuilder PRIVATE -static $<$<CONFIG:Release>:-mwindows>)
endif()
