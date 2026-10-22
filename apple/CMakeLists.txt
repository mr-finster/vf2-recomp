cmake_minimum_required(VERSION 3.24)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum macOS version")
endif()
project(VF2Apple VERSION 0.1.0 LANGUAGES C CXX OBJC OBJCXX)

if(NOT APPLE)
    message(FATAL_ERROR "This project builds the macOS and iOS apps. Use the root Makefile on other platforms.")
endif()
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O2")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O2")
set(CMAKE_OBJCXX_FLAGS_DEBUG "${CMAKE_OBJCXX_FLAGS_DEBUG} -O2")

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
include(FetchContent)
# Versions and SHA-256 values match upstream's tools/mac/deps.sh.
FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
    URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(SDL3)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
FetchContent_Declare(freetype
    URL https://downloads.sourceforge.net/project/freetype/freetype2/2.14.3/freetype-2.14.3.tar.xz
    URL_HASH SHA256=36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(freetype)

execute_process(COMMAND git describe --tags --always WORKING_DIRECTORY "${REPO_ROOT}"
    OUTPUT_VARIABLE VF2_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT VF2_VERSION)
    set(VF2_VERSION dev)
endif()
execute_process(COMMAND git log -1 --format=%cd --date=short WORKING_DIRECTORY "${REPO_ROOT}"
    OUTPUT_VARIABLE VF2_DATE OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
configure_file(version.h.in version.h @ONLY)

# The native Xcode project uses this same source manifest.
file(READ "${CMAKE_CURRENT_LIST_DIR}/sources.json" SOURCE_MANIFEST)
function(vf2_sources category output)
    string(JSON count LENGTH "${SOURCE_MANIFEST}" "${category}")
    math(EXPR last "${count} - 1")
    set(paths)
    foreach(index RANGE ${last})
        string(JSON path GET "${SOURCE_MANIFEST}" "${category}" ${index})
        list(APPEND paths "${REPO_ROOT}/${path}")
    endforeach()
    set(${output} "${paths}" PARENT_SCOPE)
endfunction()
vf2_sources(common SOURCES)
vf2_sources(ios IOS_SOURCES)
vf2_sources(macos MACOS_SOURCES)
file(GLOB RML_SOURCES CONFIGURE_DEPENDS
    "${REPO_ROOT}/third_party/rmlui/Source/Core/*.cpp"
    "${REPO_ROOT}/third_party/rmlui/Source/Core/*/*.cpp")
list(APPEND RML_SOURCES
    "${REPO_ROOT}/third_party/rmlui/Backends/RmlUi_Platform_SDL.cpp"
    "${REPO_ROOT}/third_party/rmlui/Backends/RmlUi_Renderer_SDL.cpp")

add_executable(vf2 MACOSX_BUNDLE ${SOURCES} ${RML_SOURCES})
target_include_directories(vf2 PRIVATE
    "${REPO_ROOT}/src/core/shim_include"
    "${REPO_ROOT}/third_party/rmlui/Include"
    "${REPO_ROOT}/third_party/rmlui/Backends"
    "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(vf2 PRIVATE RMLUI_STATIC_LIB RMLUI_FONT_ENGINE_FREETYPE
    RMLUI_SDL_VERSION_MAJOR=3 VF2_GPU3D_METAL=1 VF2_NATIVE_CONTROLS=1)
target_compile_options(vf2 PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
target_link_libraries(vf2 PRIVATE SDL3::SDL3-static freetype z
    "-framework Foundation" "-framework Metal" "-framework CoreVideo" "-framework QuartzCore")
set_source_files_properties(${RML_SOURCES} "${REPO_ROOT}/src/host/stb_image_impl.cpp"
    PROPERTIES COMPILE_OPTIONS -w)

set(VF2_BUNDLE_IDENTIFIER "org.vf2recomp.apple" CACHE STRING "Bundle ID for signing")
set_target_properties(vf2 PROPERTIES
    OUTPUT_NAME "VF2 Recomp"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${VF2_BUNDLE_IDENTIFIER}"
    MACOSX_BUNDLE_BUNDLE_NAME "VF2 Recomp"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "1"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${VF2_BUNDLE_IDENTIFIER}"
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic
    XCODE_ATTRIBUTE_ENABLE_USER_SCRIPT_SANDBOXING NO)

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    target_sources(vf2 PRIVATE ${IOS_SOURCES})
    target_sources(vf2 PRIVATE "${CMAKE_CURRENT_LIST_DIR}/Assets.xcassets")
    set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/Assets.xcassets"
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_compile_definitions(vf2 PRIVATE VF2_IOS=1 VF2_NO_JIT=1)
    target_link_libraries(vf2 PRIVATE "-framework UIKit" "-framework UniformTypeIdentifiers")
    set_target_properties(vf2 PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_LIST_DIR}/Info-iOS.plist.in"
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME AppIcon
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        XCODE_ATTRIBUTE_SUPPORTS_MACCATALYST NO)
else()
    add_library(sljit STATIC "${REPO_ROOT}/third_party/sljit/sljitLir.c")
    target_compile_definitions(sljit PRIVATE SLJIT_CONFIG_AUTO=1)
    target_compile_options(sljit PRIVATE -w -O2)
    target_sources(vf2 PRIVATE ${MACOS_SOURCES})
    target_link_libraries(vf2 PRIVATE sljit)
    set_target_properties(vf2 PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_LIST_DIR}/Info-macOS.plist.in")
    set(icon "${REPO_ROOT}/assets/icon/VF2.icns")
    target_sources(vf2 PRIVATE "${icon}")
    set_source_files_properties("${icon}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    enable_testing()
    add_executable(rom_import_test tests/rom_import_test.cpp "${REPO_ROOT}/src/host/romset.cpp")
    target_link_libraries(rom_import_test PRIVATE z)
    add_test(NAME rom_import COMMAND rom_import_test)
    add_executable(touchcheck tests/touchcheck.cpp)
    target_link_libraries(touchcheck PRIVATE SDL3::SDL3-static z)
    add_test(NAME touch_controls COMMAND touchcheck 2400 1080 "${CMAKE_CURRENT_BINARY_DIR}/touch_controls.png")
    set_tests_properties(touch_controls PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software")
endif()

# Explicit source resources only; never copy roms/, generated code, or build/ into an app.
file(GLOB_RECURSE ASSETS CONFIGURE_DEPENDS
    "${REPO_ROOT}/assets/ui/*" "${REPO_ROOT}/assets/fonts/*"
    "${REPO_ROOT}/assets/backgrounds/*")
foreach(asset IN LISTS ASSETS)
    file(RELATIVE_PATH relative "${REPO_ROOT}/assets" "${asset}")
    get_filename_component(folder "${relative}" DIRECTORY)
    target_sources(vf2 PRIVATE "${asset}")
    set_source_files_properties("${asset}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/assets/${folder}")
endforeach()
include("${CMAKE_CURRENT_LIST_DIR}/licenses.cmake")
