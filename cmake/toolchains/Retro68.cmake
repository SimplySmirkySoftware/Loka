set(CMAKE_SYSTEM_NAME Retro68)

# Pass these variables to try_compile
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES RETRO68_BUILD_DIR RETRO68_TOOLCHAIN_DIR RETRO68_CPU RETRO68_PPC_FLAVOR)

set(RETRO68_CPU "m68k" CACHE STRING "Retro68 target CPU (m68k or ppc)")
if(NOT RETRO68_CPU)
  set(RETRO68_CPU "m68k" CACHE STRING "Retro68 target CPU (m68k or ppc)" FORCE)
endif()
set(CMAKE_SYSTEM_PROCESSOR "${RETRO68_CPU}")

set(RETRO68_PPC_FLAVOR "retroppc" CACHE STRING "PPC toolchain flavor (retroppc only)")
# RetroCarbon is not a supported flavor (#1191): the Toolbox rail reads
# QuickDraw and Dialog Manager fields that Carbon makes opaque, and no preset
# or CI job builds it.
if(NOT RETRO68_PPC_FLAVOR STREQUAL "retroppc")
  message(FATAL_ERROR
    "RETRO68_PPC_FLAVOR=${RETRO68_PPC_FLAVOR} is not supported; use retroppc. "
    "RetroCarbon is not a supported Loka target (#1191).")
endif()

set(RETRO68_TOOLCHAIN_DIR "" CACHE PATH "Path to Retro68 toolchain CMake directory")
set(RETRO68_BUILD_DIR "" CACHE PATH "Path to Retro68 build output directory")

if(NOT RETRO68_TOOLCHAIN_DIR AND DEFINED ENV{RETRO68_TOOLCHAIN_DIR})
  set(RETRO68_TOOLCHAIN_DIR "$ENV{RETRO68_TOOLCHAIN_DIR}")
endif()

if(NOT RETRO68_BUILD_DIR AND DEFINED ENV{RETRO68_BUILD_DIR})
  set(RETRO68_BUILD_DIR "$ENV{RETRO68_BUILD_DIR}")
endif()

if(NOT RETRO68_TOOLCHAIN_DIR)
  set(_retro68_candidate_roots)
  if(RETRO68_BUILD_DIR)
    list(APPEND _retro68_candidate_roots "${RETRO68_BUILD_DIR}")
  endif()
  if(DEFINED ENV{HOME})
    list(APPEND _retro68_candidate_roots "$ENV{HOME}/Retro68-build")
    list(APPEND _retro68_candidate_roots "$ENV{HOME}/Retro68")
  endif()

  foreach(_retro68_root ${_retro68_candidate_roots})
    if(RETRO68_CPU STREQUAL "m68k")
      set(_retro68_default "${_retro68_root}/toolchain/m68k-apple-macos/cmake")
      if(EXISTS "${_retro68_default}/retro68.toolchain.cmake")
        set(RETRO68_TOOLCHAIN_DIR "${_retro68_default}")
      endif()
    elseif(RETRO68_CPU STREQUAL "ppc")
      set(_retro68_default "${_retro68_root}/toolchain/powerpc-apple-macos/cmake")
      if(EXISTS "${_retro68_default}/retroppc.toolchain.cmake")
        set(RETRO68_TOOLCHAIN_DIR "${_retro68_default}")
      endif()
    endif()
    if(RETRO68_TOOLCHAIN_DIR)
      break()
    endif()
  endforeach()
endif()

if(NOT RETRO68_TOOLCHAIN_DIR)
  message(FATAL_ERROR
    "RETRO68_TOOLCHAIN_DIR is required (path to retro68.toolchain.cmake). "
    "If unset, this toolchain checks ~/Retro68-build then ~/Retro68.")
endif()

if(RETRO68_TOOLCHAIN_DIR MATCHES "retro(68|ppc)\\.toolchain\\.cmake$")
  set(_retro68_toolchain_file "${RETRO68_TOOLCHAIN_DIR}")
else()
  if(RETRO68_CPU STREQUAL "ppc")
    set(_retro68_toolchain_file "${RETRO68_TOOLCHAIN_DIR}/retroppc.toolchain.cmake")
  else()
    set(_retro68_toolchain_file "${RETRO68_TOOLCHAIN_DIR}/retro68.toolchain.cmake")
  endif()
endif()

if(NOT EXISTS "${_retro68_toolchain_file}")
  message(FATAL_ERROR "Retro68 toolchain file not found: ${_retro68_toolchain_file}")
endif()

include("${_retro68_toolchain_file}")
