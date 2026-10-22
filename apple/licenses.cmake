# Keep the same attribution and licence text as upstream's release packages.
file(READ "${REPO_ROOT}/LICENSE" notices)
file(READ "${REPO_ROOT}/tools/release/licenses.sh" release_script)
string(REGEX MATCH "cat <<'BSD'\n([^~]*)\nBSD\n" mame_section "${release_script}")
if(NOT mame_section)
    message(FATAL_ERROR "Cannot find upstream's MAME licence notice")
endif()
string(REGEX REPLACE "^cat <<'BSD'\n|\nBSD\n$" "" mame_section "${mame_section}")
string(APPEND notices "\n\nMAME\n${mame_section}\n")
foreach(path IN ITEMS
    "${REPO_ROOT}/third_party/rmlui/LICENSE.txt"
    "${REPO_ROOT}/third_party/rmlui/Include/RmlUi/Core/Containers/LICENSE.txt"
    "${REPO_ROOT}/third_party/sljit/LICENSE"
    "${sdl3_SOURCE_DIR}/LICENSE.txt"
    "${freetype_SOURCE_DIR}/docs/FTL.TXT"
    "${REPO_ROOT}/assets/fonts/OFL.txt")
    file(READ "${path}" content)
    get_filename_component(name "${path}" NAME)
    string(APPEND notices "\n\n${name}\n${content}\n")
endforeach()
file(READ "${REPO_ROOT}/third_party/stb/stb_image.h" stb)
string(REGEX MATCH "ALTERNATIVE A - MIT License[^~]*SOFTWARE\\." stb_notice "${stb}")
string(APPEND notices "\n\nstb_image\n${stb_notice}\n")
string(APPEND notices "\nPortions of this software are copyright (c) The FreeType Project (www.freetype.org). All rights reserved.\n")
string(APPEND notices "\nTouch controls and touchcheck adapted from soulafein83/vf2-recomp, cd8b389be28fd486ee78adeb45d0ce5e1eeb6653, under the repository's MIT licence.\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/LICENSES.txt" "${notices}")
target_sources(vf2 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/LICENSES.txt")
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/LICENSES.txt" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
