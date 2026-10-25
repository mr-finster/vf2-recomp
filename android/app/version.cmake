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
file(READ "${IN}" template)
string(CONFIGURE "${template}" content @ONLY)
set(old "")
if(EXISTS "${OUT}")
    file(READ "${OUT}" old)
endif()
if(NOT old STREQUAL content)
    file(WRITE "${OUT}" "${content}")
endif()
