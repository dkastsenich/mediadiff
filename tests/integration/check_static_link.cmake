# tests/integration/check_static_link.cmake
#
# Standalone CMake script (run via `cmake -P`, never `include()`d into the
# build) that proves BUILD-04 on the shipped artifact, not on the build
# description: the executable at MEDIADIFF_BINARY must declare no dynamic
# dependency on any FFmpeg shared library.
#
# "Single static binary" here means statically linked DEPENDENCIES (FFmpeg,
# fmt, CLI11, ...) — the platform CRT/libc/C++ runtime remain dynamically
# linked on every blocking triplet by design (see
# .planning/research/STACK.md section 6, and the Windows triplet's own
# static-libs-against-dynamic-CRT naming). This script never asserts the
# total absence of dynamic linkage; it asserts the absence of FFmpeg shared
# libraries specifically.
#
# Two failure modes are handled explicitly rather than absorbed:
#   1. If the platform's inspection tool is not found, FATAL_ERROR — never
#      report success by default. A verification that quietly did not run
#      is indistinguishable from one that passed, which is the exact
#      failure this layer exists to prevent.
#   2. The inspection tool's own nonzero exit propagates as a FATAL_ERROR
#      rather than being swallowed into a default value — a tool that
#      errored produced no evidence either way.

if(NOT DEFINED MEDIADIFF_BINARY)
  message(FATAL_ERROR "check_static_link.cmake requires -DMEDIADIFF_BINARY=<path to the built executable>")
endif()

if(NOT EXISTS "${MEDIADIFF_BINARY}")
  message(FATAL_ERROR "check_static_link.cmake: MEDIADIFF_BINARY does not exist: ${MEDIADIFF_BINARY}")
endif()

# Match on the libav*/libsw* family prefixes, not a single library name, so
# a build that statically linked three of the four FFmpeg libraries and
# dynamically linked the fourth is still caught.
set(_forbidden_prefixes
  "libavcodec"
  "libavformat"
  "libavutil"
  "libavdevice"
  "libavfilter"
  "libswscale"
  "libswresample"
)

function(_fail_on_forbidden_dependency lines)
  set(_hits "")
  foreach(_line IN LISTS lines)
    foreach(_prefix IN LISTS _forbidden_prefixes)
      if(_line MATCHES "${_prefix}")
        list(APPEND _hits "${_line}")
      endif()
    endforeach()
  endforeach()
  if(_hits)
    string(REPLACE ";" "\n  " _hits_str "${_hits}")
    message(FATAL_ERROR
      "BUILD-04 violation: ${MEDIADIFF_BINARY} dynamically depends on an FFmpeg shared library:\n  ${_hits_str}")
  endif()
endfunction()

if(APPLE)
  find_program(_INSPECT_TOOL otool)
  if(NOT _INSPECT_TOOL)
    message(FATAL_ERROR
      "check_static_link.cmake: 'otool' not found on PATH — cannot verify BUILD-04 on macOS. "
      "Refusing to report success without running the check.")
  endif()
  execute_process(
    COMMAND "${_INSPECT_TOOL}" -L "${MEDIADIFF_BINARY}"
    OUTPUT_VARIABLE _tool_output
    RESULT_VARIABLE _tool_result
    ERROR_VARIABLE _tool_error
  )
  if(NOT _tool_result EQUAL 0)
    message(FATAL_ERROR
      "check_static_link.cmake: '${_INSPECT_TOOL} -L ${MEDIADIFF_BINARY}' exited ${_tool_result} — "
      "produced no evidence either way. stderr: ${_tool_error}")
  endif()
  string(REPLACE "\n" ";" _lines "${_tool_output}")
  _fail_on_forbidden_dependency("${_lines}")
elseif(WIN32)
  find_program(_INSPECT_TOOL dumpbin)
  if(NOT _INSPECT_TOOL)
    message(FATAL_ERROR
      "check_static_link.cmake: 'dumpbin' not found on PATH — cannot verify BUILD-04 on Windows. "
      "Refusing to report success without running the check.")
  endif()
  execute_process(
    COMMAND "${_INSPECT_TOOL}" /dependents "${MEDIADIFF_BINARY}"
    OUTPUT_VARIABLE _tool_output
    RESULT_VARIABLE _tool_result
    ERROR_VARIABLE _tool_error
  )
  if(NOT _tool_result EQUAL 0)
    message(FATAL_ERROR
      "check_static_link.cmake: '${_INSPECT_TOOL} /dependents ${MEDIADIFF_BINARY}' exited ${_tool_result} — "
      "produced no evidence either way. stderr: ${_tool_error}")
  endif()
  string(REPLACE "\n" ";" _lines "${_tool_output}")
  _fail_on_forbidden_dependency("${_lines}")
else()
  find_program(_INSPECT_TOOL ldd)
  if(NOT _INSPECT_TOOL)
    message(FATAL_ERROR
      "check_static_link.cmake: 'ldd' not found on PATH — cannot verify BUILD-04 on Linux. "
      "Refusing to report success without running the check.")
  endif()
  execute_process(
    COMMAND "${_INSPECT_TOOL}" "${MEDIADIFF_BINARY}"
    OUTPUT_VARIABLE _tool_output
    RESULT_VARIABLE _tool_result
    ERROR_VARIABLE _tool_error
  )
  if(NOT _tool_result EQUAL 0)
    message(FATAL_ERROR
      "check_static_link.cmake: '${_INSPECT_TOOL} ${MEDIADIFF_BINARY}' exited ${_tool_result} — "
      "produced no evidence either way. stderr: ${_tool_error}")
  endif()
  string(REPLACE "\n" ";" _lines "${_tool_output}")
  _fail_on_forbidden_dependency("${_lines}")
endif()

message(STATUS "check_static_link.cmake: ${MEDIADIFF_BINARY} declares no FFmpeg shared-library dependency.")
