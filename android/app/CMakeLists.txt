# Android (NDK/CMake) build of Virtua Fighter 2 Recomp.
# Mirrors the top-level Makefile's source lists and flags; produces libmain.so
# (the game, linked against libSDL3.so) for arm64-v8a.
cmake_minimum_required(VERSION 3.22)
project(vf2 LANGUAGES C CXX)

# The debug APK is the one that gets installed, and CMake's Debug type is -O0: the recompiled
# i960, the board, the CPU's 3D and SDL's own renderer then run at a fraction of their speed (on a
# Dimensity 9300+ the board's frame cost fell from ~20 ms to the game's own 17.4 ms budget once
# this was -O2). Keep -g, add -O2, here so the dependencies (SDL3, FreeType) get it too.
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O2")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O2")

# android/app -> the repository root
set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
get_filename_component(REPO_ROOT "${REPO_ROOT}" ABSOLUTE)

# ---------------------------------------------------------------- dependencies
# Neither SDL3 nor FreeType is in the repository: tools/android/deps.sh fetches their pinned
# release tarballs, checks them against the SHA-256 tools/mac/deps.sh pins, and unpacks them into
# build/deps/android (Gradle runs it before this configure). The versions are that script's.
set(VF2_DEPS_DIR "${REPO_ROOT}/build/deps/android" CACHE PATH
    "where tools/android/deps.sh unpacks SDL3 and FreeType")
set(VF2_SDL3_DIR "${VF2_DEPS_DIR}/SDL3-3.4.18")
set(VF2_FREETYPE_DIR "${VF2_DEPS_DIR}/freetype-2.14.3")
if(NOT EXISTS "${VF2_SDL3_DIR}/CMakeLists.txt" OR NOT EXISTS "${VF2_FREETYPE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "SDL3 and FreeType are not in ${VF2_DEPS_DIR}.\n"
        "Run tools/android/deps.sh first (Gradle does: ./gradlew :app:assembleDebug).")
endif()

# SDL3 (shared: libSDL3.so, loaded by the SDLActivity Java side).
set(SDL_SHARED ON  CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${VF2_SDL3_DIR}" "${CMAKE_BINARY_DIR}/sdl3")

# FreeType (static; RmlUi's font engine). zlib comes from the NDK sysroot.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI TRUE CACHE BOOL "" FORCE)
add_subdirectory("${VF2_FREETYPE_DIR}" "${CMAKE_BINARY_DIR}/freetype")

# SLJIT (BSD-2): the i960's native code, made at start-up (mirrors the Makefile).
add_library(sljit STATIC "${REPO_ROOT}/third_party/sljit/sljitLir.c")
target_compile_definitions(sljit PRIVATE SLJIT_CONFIG_AUTO=1)
target_compile_options(sljit PRIVATE -w -O2)

# ------------------------------------------------------------------- version.h
# Made at every build, not only at configure time: a new commit must change the
# version and the BUILD that a GPU failure is remembered by (src/host/main.cpp),
# and Gradle doesn't reconfigure CMake just because git moved. The script writes
# the file only when its content changes, so nothing is rebuilt needlessly.
set(VF2_VERSION_H "${CMAKE_CURRENT_BINARY_DIR}/version.h")
add_custom_target(vf2_version ALL
    COMMAND ${CMAKE_COMMAND}
        -DREPO_ROOT=${REPO_ROOT}
        -DIN=${CMAKE_CURRENT_LIST_DIR}/version.h.in
        -DOUT=${VF2_VERSION_H}
        -P ${CMAKE_CURRENT_LIST_DIR}/version.cmake
    BYPRODUCTS ${VF2_VERSION_H}
    COMMENT "version.h from git")

# ------------------------------------------------------------- the game's code
set(VF2_APP
    src/host/main.cpp src/host/menu.cpp src/host/sysinfo.cpp
    src/host/d3d11_map.cpp src/host/stb_image_impl.cpp
    src/rec/gen_none.cpp
    src/gpu/gpu3d_sdlgpu.cpp)                 # SDL_GPU (Vulkan on Android)
set(VF2_MACHINE
    src/machine/model2a.cpp src/machine/model2_video.cpp
    src/machine/model2_smooth.cpp src/machine/scsp.cpp src/machine/scspdsp.cpp)
set(VF2_HOST
    src/host/romset.cpp src/host/input_script.cpp)
set(VF2_CORE
    third_party/mame/i960/i960.cpp third_party/mame/i960/i960dis.cpp
    src/core/mame_shim.cpp src/core/mb86233.cpp src/core/i960_device.cpp
    src/rec/i960_rec.cpp src/rec/i960_analyse.cpp
    src/rec/i960_native.cpp src/rec/vf2_map.cpp)
set(VF2_M68000
    third_party/mame/m68000/m68000.cpp third_party/mame/m68000/m68000-decode.cpp
    third_party/mame/m68000/m68000-sdf.cpp third_party/mame/m68000/m68000-sdp.cpp)
file(GLOB VF2_RML
    "${REPO_ROOT}/third_party/rmlui/Source/Core/*.cpp"
    "${REPO_ROOT}/third_party/rmlui/Source/Core/*/*.cpp")
list(APPEND VF2_RML
    "${REPO_ROOT}/third_party/rmlui/Backends/RmlUi_Platform_SDL.cpp"
    "${REPO_ROOT}/third_party/rmlui/Backends/RmlUi_Renderer_SDL.cpp")

# Prefix the repository-relative lists with REPO_ROOT.
foreach(group VF2_APP VF2_MACHINE VF2_HOST VF2_CORE VF2_M68000)
    set(abs "")
    foreach(f ${${group}})
        list(APPEND abs "${REPO_ROOT}/${f}")
    endforeach()
    set(${group} "${abs}")
endforeach()

add_library(main SHARED
    ${VF2_APP} ${VF2_MACHINE} ${VF2_HOST} ${VF2_CORE} ${VF2_M68000} ${VF2_RML})
add_dependencies(main vf2_version)

target_compile_features(main PRIVATE cxx_std_20)
target_compile_definitions(main PRIVATE
    RMLUI_STATIC_LIB
    RMLUI_FONT_ENGINE_FREETYPE
    RMLUI_SDL_VERSION_MAJOR=3
    VF2_GPU3D_SDL=1)
target_include_directories(main PRIVATE
    "${REPO_ROOT}/src/core/shim_include"
    "${REPO_ROOT}/third_party/rmlui/Include"
    "${REPO_ROOT}/third_party/rmlui/Backends"
    "${VF2_FREETYPE_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}")            # version.h

# RmlUi and stb are third-party: no warnings (as in the Makefile).
set_source_files_properties(${VF2_RML} "${REPO_ROOT}/src/host/stb_image_impl.cpp"
    PROPERTIES COMPILE_OPTIONS "-w")

target_link_libraries(main PRIVATE
    SDL3::SDL3
    freetype
    sljit
    z log android)
