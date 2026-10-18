cmake_minimum_required(VERSION 3.15)
project(LokaShowcase LANGUAGES CXX)

if(APPLE)
  enable_language(OBJCXX)
  loka_suppress_arclite_link()
endif()

if(NOT WIN32 AND NOT APPLE AND NOT (CMAKE_SYSTEM_NAME MATCHES "^Retro(68|PPC|Carbon)$"))
  add_custom_target(LokaShowcase ALL
    COMMAND ${CMAKE_COMMAND} -E echo "LokaShowcase has no supported platform target here."
  )
  return()
endif()

if(WIN32)
  include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/LokaWin32VisualStyles.cmake)
  set(SHOWCASE_SOURCES
    src/main.cpp
  )

  if(NOT TARGET LokaWin32Core)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../win32 ${CMAKE_CURRENT_BINARY_DIR}/win32)
  endif()

  add_executable(LokaShowcaseWin32 WIN32 ${SHOWCASE_SOURCES})
  loka_enable_warnings(LokaShowcaseWin32)
  loka_enable_win32_visual_styles(LokaShowcaseWin32)

  target_include_directories(LokaShowcaseWin32 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../../win32/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )

  target_link_libraries(LokaShowcaseWin32 PRIVATE LokaWin32Core)

  if(MSVC)
    target_compile_options(LokaShowcaseWin32 PRIVATE /utf-8)
  endif()
endif()

if(APPLE)
  set(MACOS_SOURCES
    src/MacOSMain.mm
  )
  file(GLOB_RECURSE MACOS_HEADERS src/*.hpp)

  add_executable(LokaShowcaseMacOS ${MACOS_SOURCES} ${MACOS_HEADERS})
  loka_enable_warnings(LokaShowcaseMacOS)

  target_include_directories(LokaShowcaseMacOS PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apple/macos/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apple/macos/src/context
  )

  target_link_libraries(LokaShowcaseMacOS PRIVATE LokaMacOSCore)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND LOKA_MACOS_EXPLICIT_NO_ARC_FLAGS)
    target_compile_options(LokaShowcaseMacOS PRIVATE "-fno-objc-arc")
  endif()
endif()

if(CMAKE_SYSTEM_NAME MATCHES "^Retro(68|PPC|Carbon)$")
  set(LOKA_SHOWCASE_CLASSIC_TARGET "LokaShowcase${LOKA_CLASSIC_TARGET_SUFFIX}")
  set(RETRO_SOURCES
    src/main.cpp
    Size.r
  )

  add_application(${LOKA_SHOWCASE_CLASSIC_TARGET} ${RETRO_SOURCES})
  loka_enable_warnings(${LOKA_SHOWCASE_CLASSIC_TARGET})

  target_include_directories(${LOKA_SHOWCASE_CLASSIC_TARGET} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )

  target_link_libraries(${LOKA_SHOWCASE_CLASSIC_TARGET} PRIVATE LokaAppleToolboxCore)

  target_compile_definitions(${LOKA_SHOWCASE_CLASSIC_TARGET} PRIVATE LOKA_RETRO68=1)

  set_target_properties(${LOKA_SHOWCASE_CLASSIC_TARGET} PROPERTIES
    CXX_STANDARD 98
    CXX_STANDARD_REQUIRED YES
  )

  target_compile_options(${LOKA_SHOWCASE_CLASSIC_TARGET} PRIVATE
    -Os
    -ffunction-sections
    -fdata-sections
    -fno-exceptions
    -fno-rtti
  )
  target_link_options(${LOKA_SHOWCASE_CLASSIC_TARGET} PRIVATE
    -Wl,--gc-sections
  )

  # Compact shipping profile (#135): drop the MacsBug name sections from the
  # CODE resources. Elf2Mac-only (68K); the DWARF .gdb ELF keeps full symbols
  # for the MAME gdbstub workflow.
  if(CMAKE_SYSTEM_NAME STREQUAL "Retro68" AND NOT LOKA_RETRO68_DIAGNOSTICS)
    target_link_options(${LOKA_SHOWCASE_CLASSIC_TARGET} PRIVATE -Wl,--mac-strip-macsbug)
  endif()

  option(LOKA_CLASSIC_STRIP "Strip Classic Mac binaries after link" ON)
  if(LOKA_CLASSIC_STRIP)
    find_program(RETRO68_STRIP
      NAMES m68k-apple-macos-strip powerpc-apple-macos-strip
      HINTS
        "$ENV{RETRO68_BUILD_DIR}/toolchain/bin"
        "${RETRO68_BUILD_DIR}/toolchain/bin"
        "${RETRO68_TOOLCHAIN_DIR}/../bin"
    )
    if(RETRO68_STRIP)
      add_custom_command(TARGET ${LOKA_SHOWCASE_CLASSIC_TARGET} POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
          -DSTRIP_TOOL="${RETRO68_STRIP}"
          -DSTRIP_INPUT="$<TARGET_FILE:${LOKA_SHOWCASE_CLASSIC_TARGET}>.gdb"
          -P "${CMAKE_SOURCE_DIR}/cmake/strip_classic.cmake"
        COMMENT "Stripping Classic debug binary"
      )
    endif()
  endif()
endif()
