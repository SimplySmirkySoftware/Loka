cmake_minimum_required(VERSION 3.15)
project(LokaSimpleText LANGUAGES CXX)

if(APPLE)
  enable_language(OBJCXX)
  loka_suppress_arclite_link()
endif()

if(NOT WIN32 AND NOT APPLE AND NOT (CMAKE_SYSTEM_NAME MATCHES "^Retro(68|PPC|Carbon)$"))
  add_custom_target(LokaSimpleText ALL
    COMMAND ${CMAKE_COMMAND} -E echo "LokaSimpleText has no supported platform target here."
  )
  return()
endif()

if(WIN32)
  include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/LokaWin32VisualStyles.cmake)
  set(SIMPLETEXT_SOURCES
    src/main.cpp
  )

  if(NOT TARGET LokaWin32Core)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../win32 ${CMAKE_CURRENT_BINARY_DIR}/win32)
  endif()

  add_executable(LokaSimpleTextWin32 WIN32 ${SIMPLETEXT_SOURCES})
  loka_enable_warnings(LokaSimpleTextWin32)
  loka_enable_win32_visual_styles(LokaSimpleTextWin32)

  target_include_directories(LokaSimpleTextWin32 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../../win32/src
  )

  target_link_libraries(LokaSimpleTextWin32 PRIVATE LokaWin32Core)

  if(MSVC)
    target_compile_options(LokaSimpleTextWin32 PRIVATE /utf-8)
  endif()
endif()

if(APPLE)
  set(MACOS_SOURCES
    src/MacOSMain.mm
  )
  file(GLOB_RECURSE MACOS_HEADERS src/*.hpp)

  add_executable(LokaSimpleTextMacOS MACOSX_BUNDLE ${MACOS_SOURCES} ${MACOS_HEADERS})
  loka_enable_warnings(LokaSimpleTextMacOS)
  set_target_properties(LokaSimpleTextMacOS PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.loka.simpletext"
    MACOSX_BUNDLE_BUNDLE_NAME "LokaSimpleTextMacOS"
  )

  target_include_directories(LokaSimpleTextMacOS PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apple/macos/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apple/macos/src/context
  )

  target_link_libraries(LokaSimpleTextMacOS PRIVATE LokaMacOSCore)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND LOKA_MACOS_EXPLICIT_NO_ARC_FLAGS)
    target_compile_options(LokaSimpleTextMacOS PRIVATE "-fno-objc-arc")
  endif()
endif()

if(CMAKE_SYSTEM_NAME MATCHES "^Retro(68|PPC|Carbon)$")
  set(LOKA_SIMPLETEXT_CLASSIC_TARGET "LokaSimpleText${LOKA_CLASSIC_TARGET_SUFFIX}")
  set(RETRO_SOURCES
    src/main.cpp
    Size.r
  )

  add_application(${LOKA_SIMPLETEXT_CLASSIC_TARGET} ${RETRO_SOURCES})
  loka_enable_warnings(${LOKA_SIMPLETEXT_CLASSIC_TARGET})

  target_include_directories(${LOKA_SIMPLETEXT_CLASSIC_TARGET} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
  )

  target_link_libraries(${LOKA_SIMPLETEXT_CLASSIC_TARGET} PRIVATE LokaAppleToolboxCore)

  target_compile_definitions(${LOKA_SIMPLETEXT_CLASSIC_TARGET} PRIVATE LOKA_RETRO68=1)

  set_target_properties(${LOKA_SIMPLETEXT_CLASSIC_TARGET} PROPERTIES
    CXX_STANDARD 98
    CXX_STANDARD_REQUIRED YES
  )

  target_compile_options(${LOKA_SIMPLETEXT_CLASSIC_TARGET} PRIVATE
    -Os
    -ffunction-sections
    -fdata-sections
    -fno-exceptions
    -fno-rtti
  )
  target_link_options(${LOKA_SIMPLETEXT_CLASSIC_TARGET} PRIVATE
    -Wl,--gc-sections
  )

  # Compact shipping profile (#135): drop the MacsBug name sections from the
  # CODE resources. Elf2Mac-only (68K); the DWARF .gdb ELF keeps full symbols
  # for the MAME gdbstub workflow.
  if(CMAKE_SYSTEM_NAME STREQUAL "Retro68" AND NOT LOKA_RETRO68_DIAGNOSTICS)
    target_link_options(${LOKA_SIMPLETEXT_CLASSIC_TARGET} PRIVATE -Wl,--mac-strip-macsbug)
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
      add_custom_command(TARGET ${LOKA_SIMPLETEXT_CLASSIC_TARGET} POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
          -DSTRIP_TOOL="${RETRO68_STRIP}"
          -DSTRIP_INPUT="$<TARGET_FILE:${LOKA_SIMPLETEXT_CLASSIC_TARGET}>.gdb"
          -P "${CMAKE_SOURCE_DIR}/cmake/strip_classic.cmake"
        COMMENT "Stripping Classic debug binary"
      )
    endif()
  endif()
endif()

# Deliberate acceptance twin of the Classic target above; keep settings aligned.
if(CMAKE_SYSTEM_NAME STREQUAL "RetroPPC")
  # add_application shares Rez outputs by source basename. Give the Unicode
  # partition its own resource source; C++ target definitions do not reach Rez.
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Size.r")
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/UnicodeSize.r"
    "#define LOKA_TOOLBOX_UNICODE 1\n#include \"${CMAKE_CURRENT_SOURCE_DIR}/Size.r\"\n")
  add_application(LokaSimpleTextUnicodePPC src/main.cpp "${CMAKE_CURRENT_BINARY_DIR}/UnicodeSize.r")
  loka_enable_warnings(LokaSimpleTextUnicodePPC)

  target_include_directories(LokaSimpleTextUnicodePPC PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
  )

  loka_toolbox_unicode(LokaSimpleTextUnicodePPC)

  target_compile_definitions(LokaSimpleTextUnicodePPC PRIVATE LOKA_RETRO68=1)

  set_target_properties(LokaSimpleTextUnicodePPC PROPERTIES
    CXX_STANDARD 98
    CXX_STANDARD_REQUIRED YES
  )

  target_compile_options(LokaSimpleTextUnicodePPC PRIVATE
    -Os
    -ffunction-sections
    -fdata-sections
    -fno-exceptions
    -fno-rtti
  )
  target_link_options(LokaSimpleTextUnicodePPC PRIVATE
    -Wl,--gc-sections
  )

endif()
