# Makes version.h from git at every build (see CMakeLists.txt). Rewritten only when
# its content changes, so a build that didn't move doesn't recompile main.cpp.
execute_process(COMMAND git describe --tags --always
    WORKING_DIRECTORY "${REPO_ROOT}"
    OUTPUT_VARIABLE VF2_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT VF2_VERSION)
    set(VF2_VERSION "dev")
endif()
execute_process(COMMAND git log -1 --format=%cd --date=short
    WORKING_DIRECTORY "${REPO_ROOT}"
    OUTPUT_VARIABLE VF2_DATE OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT VF2_DATE)
    set(VF2_DATE "unknown")
endif()
# The commit too: what a GPU failure is remembered by (src/host/main.cpp's BUILD).
execute_process(COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY "${REPO_ROOT}"
    OUTPUT_VARIABLE VF2_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT VF2_COMMIT)
    set(VF2_COMMIT "unknown")
endif()
# The lobby Online's room codes use (VF3's, shared): .lobby-url's one line, or the environment's
# (the Makefile's way). Empty or "none": no lobby built in, and Online plays by address only.
if(DEFINED ENV{VF_LOBBY_URL} AND NOT "$ENV{VF_LOBBY_URL}" STREQUAL "")
    set(VF_LOBBY_URL "$ENV{VF_LOBBY_URL}")
elseif(EXISTS "${REPO_ROOT}/.lobby-url")
    file(READ "${REPO_ROOT}/.lobby-url" VF_LOBBY_URL)
    string(STRIP "${VF_LOBBY_URL}" VF_LOBBY_URL)
else()
    set(VF_LOBBY_URL "")
endif()
if(VF_LOBBY_URL STREQUAL "none")
    set(VF_LOBBY_URL "")
endif()
file(READ "${IN}" template)
string(CONFIGURE "${template}" content @ONLY)
set(old "")
if(EXISTS "${OUT}")
    file(READ "${OUT}" old)
endif()
if(NOT old STREQUAL content)
    file(WRITE "${OUT}" "${content}")
endif()
