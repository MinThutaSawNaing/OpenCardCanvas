# ---------------------------------------------------------------------------
# DeployQt.cmake - runs windeployqt so a self-contained portable distribution is
# produced.
#
#     cmake --build . --target portable
#
# The distribution is written to <build>/dist, NOT into <build>/bin. That
# separation matters: a deployed "platforms" folder next to the build output
# would shadow Qt's own plugin directory and stop the offscreen tests from
# finding the plugin they run under.
#
# Requires -DQT_BIN_DIR=<dir> -DAPP_DIR=<build output dir> -DDIST_DIR=<dir>
# ---------------------------------------------------------------------------
if(NOT DEFINED QT_BIN_DIR OR NOT DEFINED APP_DIR OR NOT DEFINED DIST_DIR)
    message(FATAL_ERROR "DeployQt.cmake requires -DQT_BIN_DIR=<dir> -DAPP_DIR=<dir> -DDIST_DIR=<dir>")
endif()

find_program(WINDEPLOYQT_EXECUTABLE windeployqt
    HINTS "${QT_BIN_DIR}"
    NO_DEFAULT_PATH)
if(NOT WINDEPLOYQT_EXECUTABLE)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT_BIN_DIR}")
endif()
if(NOT WINDEPLOYQT_EXECUTABLE)
    message(FATAL_ERROR "windeployqt not found in ${QT_BIN_DIR}")
endif()

set(_src_exe "${APP_DIR}/OpenCardCanvas.exe")
if(NOT EXISTS "${_src_exe}")
    message(FATAL_ERROR "Application executable not found: ${_src_exe}")
endif()

file(MAKE_DIRECTORY "${DIST_DIR}")
file(COPY "${_src_exe}" DESTINATION "${DIST_DIR}")
set(_exe "${DIST_DIR}/OpenCardCanvas.exe")

message(STATUS "Deploying Qt runtime with ${WINDEPLOYQT_EXECUTABLE}")
set(_deploy_mode --release)
if(DEPLOY_CONFIG STREQUAL "Debug")
    set(_deploy_mode --debug)
endif()
# windeployqt inspects the executable and pulls in exactly the modules and
# plugins that are needed, so no explicit module flags are passed here (several
# that older versions accepted no longer exist, e.g. --print-support).
execute_process(
    COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            ${_deploy_mode}
            --no-translations
            --no-compiler-runtime
            --dir "${DIST_DIR}"
            "${_exe}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err)
message(STATUS "${_out}")
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "windeployqt failed (${_rc}): ${_err}")
endif()

# Ship the documentation alongside the executable so the portable copy is as
# usable as the installed one.
foreach(_doc README.md LICENSE)
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../${_doc}")
        file(COPY "${CMAKE_CURRENT_LIST_DIR}/../${_doc}" DESTINATION "${DIST_DIR}")
    endif()
endforeach()

message(STATUS "Portable distribution ready in ${DIST_DIR}")
