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
# SDL3, FreeType, mbedTLS and curl are not in the repository: tools/android/deps.sh fetches their
# pinned release tarballs, checks the SHA-256 (SDL3's and FreeType's the same as tools/mac/deps.sh
# pins), and unpacks them into build/deps/android (Gradle runs it before this configure). The
# versions are that script's.
set(VF2_DEPS_DIR "${REPO_ROOT}/build/deps/android" CACHE PATH
    "where tools/android/deps.sh unpacks SDL3, FreeType, mbedTLS and curl")
set(VF2_SDL3_DIR "${VF2_DEPS_DIR}/SDL3-3.4.18")
set(VF2_FREETYPE_DIR "${VF2_DEPS_DIR}/freetype-2.14.3")
set(VF2_MBEDTLS_DIR "${VF2_DEPS_DIR}/mbedtls-3.6.7")
set(VF2_CURL_DIR "${VF2_DEPS_DIR}/curl-8.23.0")
if(NOT EXISTS "${VF2_SDL3_DIR}/CMakeLists.txt" OR NOT EXISTS "${VF2_FREETYPE_DIR}/CMakeLists.txt"
   OR NOT EXISTS "${VF2_MBEDTLS_DIR}/CMakeLists.txt" OR NOT EXISTS "${VF2_CURL_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "SDL3, FreeType, mbedTLS and curl are not in ${VF2_DEPS_DIR}.\n"
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

# mbedTLS (static; curl's TLS backend). Built before curl, whose FindMbedTLS is handed the include
# dir and the three archive paths: it uses a variable that's already set instead of searching for
# an installed mbedTLS, which a subdirectory build never installs.
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
add_subdirectory("${VF2_MBEDTLS_DIR}" "${CMAKE_BINARY_DIR}/mbedtls")
set(MBEDTLS_INCLUDE_DIR "${VF2_MBEDTLS_DIR}/include" CACHE STRING "" FORCE)
set(MBEDTLS_LIBRARY    "${CMAKE_BINARY_DIR}/mbedtls/library/libmbedtls.a" CACHE FILEPATH "" FORCE)
set(MBEDX509_LIBRARY   "${CMAKE_BINARY_DIR}/mbedtls/library/libmbedx509.a" CACHE FILEPATH "" FORCE)
set(MBEDCRYPTO_LIBRARY "${CMAKE_BINARY_DIR}/mbedtls/library/libmbedcrypto.a" CACHE FILEPATH "" FORCE)

# curl (shared: libcurl.so, the lobby's HTTPS). src/net/http.cpp loads libcurl by name as it runs
# (the desktop's system one the same way), so nothing links it here: the APK just ships the .so,
# and the game says "no lobby" when the build carries no address (VF_LOBBY_URL). Only HTTP(S) and
# the WebSockets watching uses are wanted; the other protocols and the optional libraries (brotli,
# zstd, libpsl, nghttp2, idn, SSH, LDAP) stay out, so nothing extra is needed to build it.
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS OFF CACHE BOOL "" FORCE)
# curl's own default names a debug build's library libcurl-d.so; src/net/http.cpp loads "libcurl.so"
# by name, so the postfix is dropped and both build types ship the same name.
set(CMAKE_DEBUG_POSTFIX "" CACHE STRING "" FORCE)
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CURL_USE_MBEDTLS ON CACHE BOOL "" FORCE)
set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
set(CURL_BROTLI OFF CACHE BOOL "" FORCE)
set(CURL_ZSTD OFF CACHE BOOL "" FORCE)
set(USE_NGHTTP2 OFF CACHE BOOL "" FORCE)
# Android has no single CA file, but its trust store is a directory of hashed certificates
# (/system/etc/security/cacerts, world readable since Android 1.0). Point libcurl's default CA path
# at it: the mbedTLS backend reads the directory
# (mbedtls_x509_crt_parse_path), so HTTPS to the lobby verifies with nothing to ship.
set(CURL_CA_PATH "/system/etc/security/cacerts" CACHE STRING "" FORCE)
set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAPS ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_DICT ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_FILE ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_FTP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_GOPHER ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_IMAP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_MQTT ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_POP3 ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_RTSP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_SMTP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_TELNET ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_TFTP ON CACHE BOOL "" FORCE)
add_subdirectory("${VF2_CURL_DIR}" "${CMAKE_BINARY_DIR}/curl")

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
    src/host/main.cpp src/host/menu.cpp src/host/online.cpp src/host/i18n.cpp src/host/text.cpp src/host/links.cpp src/host/vf2_netplay.cpp
    src/host/sysinfo.cpp
    src/host/d3d11_map.cpp src/host/stb_image_impl.cpp
    src/rec/gen_none.cpp
    src/gpu/gpu3d_sdlgpu.cpp)                 # SDL_GPU (Vulkan on Android)
set(VF2_MACHINE
    src/machine/model2a.cpp src/machine/random_fighter.cpp src/machine/hud_names.cpp
    src/machine/model2_video.cpp
    src/machine/model2_smooth.cpp src/machine/scsp.cpp src/machine/scspdsp.cpp)
set(VF2_HOST
    src/host/romset.cpp src/host/input_script.cpp)
# Netplay (src/net, VF3's, kept the same in all three): the game's Online play
set(VF2_NET
    src/net/session.cpp src/net/link.cpp src/net/room_link.cpp
    src/net/stun.cpp src/net/http.cpp src/net/watch.cpp)
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
foreach(group VF2_APP VF2_MACHINE VF2_HOST VF2_NET VF2_CORE VF2_M68000)
    set(abs "")
    foreach(f ${${group}})
        list(APPEND abs "${REPO_ROOT}/${f}")
    endforeach()
    set(${group} "${abs}")
endforeach()

# The game's own float arithmetic (the i960's floating point in the interpreter and the native
# code's helpers, the TGP's ALU, the board): one rounding an operation on every machine, so that
# two in a netplay match run the same game (arm64's clang would fuse a * b + c otherwise). Not the
# renderer's, which is the picture's alone.
set_source_files_properties(${VF2_CORE}
    "${REPO_ROOT}/src/machine/model2a.cpp"
    "${REPO_ROOT}/src/machine/random_fighter.cpp"
    PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")

add_library(main SHARED
    ${VF2_APP} ${VF2_MACHINE} ${VF2_HOST} ${VF2_NET} ${VF2_CORE} ${VF2_M68000} ${VF2_RML})
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
    z log android dl)
