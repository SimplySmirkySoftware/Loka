cmake_minimum_required(VERSION 3.15)
project(LokaCardShowcase LANGUAGES CXX)

if(APPLE)
  enable_language(OBJCXX)
  loka_suppress_arclite_link()
endif()

if(NOT WIN32 AND NOT APPLE AND NOT (CMAKE_SYSTEM_NAME MATCHES "^Retro(68|PPC|Carbon)$"))
  add_custom_target(LokaCardShowcase ALL
    COMMAND ${CMAKE_COMMAND} -E echo "LokaCardShowcase has no supported platform target here."
  )
  return()
endif()

set(CARDSHOWCASE_PHOTOS
  ${CMAKE_CURRENT_SOURCE_DIR}/../../assets/LokaLaptops.jpg
  ${CMAKE_CURRENT_SOURCE_DIR}/../../goodies/LokaFlyer_0.0.1_PBG4.jpg
)

if(WIN32)
  include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/LokaWin32VisualStyles.cmake)
  set(CARDSHOWCASE_SOURCES
    src/main.cpp
  )

  if(NOT TARGET LokaWin32Core)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../win32 ${CMAKE_CURRENT_BINARY_DIR}/win32)
  endif()

  add_executable(LokaCardShowcaseWin32 WIN32 ${CARDSHOWCASE_SOURCES})
  loka_enable_warnings(LokaCardShowcaseWin32)
  loka_enable_win32_visual_styles(LokaCardShowcaseWin32)

  target_include_directories(LokaCardShowcaseWin32 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../../win32/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )

  target_link_libraries(LokaCardShowcaseWin32 PRIVATE LokaWin32Core)
  add_custom_command(TARGET LokaCardShowcaseWin32 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      ${CARDSHOWCASE_PHOTOS} $<TARGET_FILE_DIR:LokaCardShowcaseWin32>
    VERBATIM
  )

  if(MSVC)
    target_compile_options(LokaCardShowcaseWin32 PRIVATE /utf-8)
  endif()
endif()

if(APPLE)
  set(MACOS_SOURCES
    src/MacOSMain.mm
  )
  file(GLOB_RECURSE MACOS_HEADERS src/*.hpp)

  set(CARDSHOWCASE_MACOS_RESOURCES)
  foreach(photo IN LISTS CARDSHOWCASE_PHOTOS)
    get_filename_component(photo_name "${photo}" NAME)
    set(resource "${CMAKE_CURRENT_BINARY_DIR}/macos-resources/${photo_name}")
    configure_file("${photo}" "${resource}" COPYONLY)
    set_source_files_properties("${resource}" PROPERTIES
      MACOSX_PACKAGE_LOCATION Resources
    )
    list(APPEND CARDSHOWCASE_MACOS_RESOURCES "${resource}")
  endforeach()
  add_executable(LokaCardShowcaseMacOS MACOSX_BUNDLE
    ${MACOS_SOURCES} ${MACOS_HEADERS} ${CARDSHOWCASE_MACOS_RESOURCES})
  set_target_properties(LokaCardShowcaseMacOS PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.loka.cardshowcase"
    MACOSX_BUNDLE_BUNDLE_NAME "LokaCardShowcaseMacOS"
  )
  loka_enable_warnings(LokaCardShowcaseMacOS)

  target_include_directories(LokaCardShowcaseMacOS PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apple/macos/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apple/macos/src/context
  )

  target_link_libraries(LokaCardShowcaseMacOS PRIVATE LokaMacOSCore)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND LOKA_MACOS_EXPLICIT_NO_ARC_FLAGS)
    target_compile_options(LokaCardShowcaseMacOS PRIVATE "-fno-objc-arc")
  endif()
endif()

if(CMAKE_SYSTEM_NAME MATCHES "^Retro(68|PPC|Carbon)$")
  set(LOKA_CARDSHOWCASE_CLASSIC_TARGET "LokaCardShowcase${LOKA_CLASSIC_TARGET_SUFFIX}")
  set(RETRO_SOURCES
    src/main.cpp
    Size.r
  )

  add_application(${LOKA_CARDSHOWCASE_CLASSIC_TARGET} ${RETRO_SOURCES})
  loka_enable_warnings(${LOKA_CARDSHOWCASE_CLASSIC_TARGET})

  target_include_directories(${LOKA_CARDSHOWCASE_CLASSIC_TARGET} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )

  target_link_libraries(${LOKA_CARDSHOWCASE_CLASSIC_TARGET} PRIVATE LokaAppleToolboxCore)

  target_compile_definitions(${LOKA_CARDSHOWCASE_CLASSIC_TARGET} PRIVATE LOKA_RETRO68=1)

  set_target_properties(${LOKA_CARDSHOWCASE_CLASSIC_TARGET} PROPERTIES
    CXX_STANDARD 98
    CXX_STANDARD_REQUIRED YES
  )

  target_compile_options(${LOKA_CARDSHOWCASE_CLASSIC_TARGET} PRIVATE
    -Os
    -ffunction-sections
    -fdata-sections
    -fno-exceptions
    -fno-rtti
  )
  target_link_options(${LOKA_CARDSHOWCASE_CLASSIC_TARGET} PRIVATE
    -Wl,--gc-sections
  )

  # Compact shipping profile (#135): drop the MacsBug name sections from the
  # CODE resources. Elf2Mac-only (68K); the DWARF .gdb ELF keeps full symbols
  # for the MAME gdbstub workflow.
  if(CMAKE_SYSTEM_NAME STREQUAL "Retro68" AND NOT LOKA_RETRO68_DIAGNOSTICS)
    target_link_options(${LOKA_CARDSHOWCASE_CLASSIC_TARGET} PRIVATE -Wl,--mac-strip-macsbug)
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
      add_custom_command(TARGET ${LOKA_CARDSHOWCASE_CLASSIC_TARGET} POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
          -DSTRIP_TOOL="${RETRO68_STRIP}"
          -DSTRIP_INPUT="$<TARGET_FILE:${LOKA_CARDSHOWCASE_CLASSIC_TARGET}>.gdb"
          -P "${CMAKE_SOURCE_DIR}/cmake/strip_classic.cmake"
        COMMENT "Stripping Classic debug binary"
      )
    endif()
  endif()
endif()
