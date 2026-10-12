cmake_minimum_required(VERSION 3.15)
project(ScrapbookUI LANGUAGES CXX)

if(APPLE)
  enable_language(OBJCXX)
  loka_suppress_arclite_link()
endif()

if(NOT WIN32 AND NOT APPLE AND NOT LOKA_CLASSIC_MAC)
  add_custom_target(ScrapbookUI ALL
    COMMAND ${CMAKE_COMMAND} -E echo
      "ScrapbookUI has no supported platform target here."
  )
  return()
endif()

set(SCRAPBOOK_COMMON_SOURCES
  src/main.cpp
  src/ScrapbookPackage.cpp
)

if(WIN32)
  include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/LokaWin32VisualStyles.cmake)
  if(NOT TARGET LokaWin32Core)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../win32 ${CMAKE_CURRENT_BINARY_DIR}/win32)
  endif()

  add_executable(ScrapbookUIWin32 WIN32 ${SCRAPBOOK_COMMON_SOURCES})
  loka_enable_warnings(ScrapbookUIWin32)
  loka_enable_win32_visual_styles(ScrapbookUIWin32)

  target_include_directories(ScrapbookUIWin32 PRIVATE
    ${LOKA_SCRAPBOOK_R_HEADER_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../win32/src
  )
  target_link_libraries(ScrapbookUIWin32 PRIVATE LokaWin32Core)
  set_target_properties(ScrapbookUIWin32 PROPERTIES
    CXX_STANDARD 98
    CXX_STANDARD_REQUIRED YES
  )
  if(MSVC)
    target_compile_options(ScrapbookUIWin32 PRIVATE /utf-8)
  endif()

  # The source package is environment-specific; the runtime contract is not.
  configure_file(assets/ASSETS-modern.LRP ASSETS.LRP COPYONLY)
endif()

if(APPLE)
  set(SCRAPBOOK_MACOS_RESOURCE_DIR
    ${CMAKE_CURRENT_BINARY_DIR}/macos-resources
  )
  file(MAKE_DIRECTORY ${SCRAPBOOK_MACOS_RESOURCE_DIR})
  configure_file(
    assets/ASSETS-modern.LRP
    ${SCRAPBOOK_MACOS_RESOURCE_DIR}/ASSETS.LRP
    COPYONLY
  )
  set_source_files_properties(
    ${SCRAPBOOK_MACOS_RESOURCE_DIR}/ASSETS.LRP
    PROPERTIES
      GENERATED TRUE
      MACOSX_PACKAGE_LOCATION Resources
  )

  file(GLOB_RECURSE SCRAPBOOK_MACOS_HEADERS src/*.hpp)
  add_executable(ScrapbookUIMacOS MACOSX_BUNDLE
    src/MacOSMain.mm
    src/ScrapbookPackage.cpp
    ${SCRAPBOOK_MACOS_HEADERS}
    ${SCRAPBOOK_MACOS_RESOURCE_DIR}/ASSETS.LRP
  )
  loka_enable_warnings(ScrapbookUIMacOS)

  set_target_properties(ScrapbookUIMacOS PROPERTIES
    CXX_STANDARD 98
    CXX_STANDARD_REQUIRED YES
    OBJCXX_STANDARD 98
    OBJCXX_STANDARD_REQUIRED YES
    MACOSX_BUNDLE_BUNDLE_NAME "ScrapbookUIMacOS"
  )

  target_include_directories(ScrapbookUIMacOS PRIVATE
    ${LOKA_SCRAPBOOK_R_HEADER_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apple/macos/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apple/macos/src/context
  )
  target_link_libraries(ScrapbookUIMacOS PRIVATE LokaMacOSCore)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND LOKA_MACOS_EXPLICIT_NO_ARC_FLAGS)
    target_compile_options(ScrapbookUIMacOS PRIVATE "-fno-objc-arc")
  endif()
endif()

if(NOT LOKA_CLASSIC_MAC)
  return()
endif()

set(SCRAPBOOK_CLASSIC_TARGET "ScrapbookUI${LOKA_CLASSIC_TARGET_SUFFIX}")
set(SCRAPBOOK_SOURCES
  ${SCRAPBOOK_COMMON_SOURCES}
  Size.r
)

add_application(${SCRAPBOOK_CLASSIC_TARGET} ${SCRAPBOOK_SOURCES})
loka_enable_warnings(${SCRAPBOOK_CLASSIC_TARGET})

target_include_directories(${SCRAPBOOK_CLASSIC_TARGET} PRIVATE
  ${LOKA_SCRAPBOOK_R_HEADER_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../../common
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/../../apple/toolbox/src
)

target_link_libraries(${SCRAPBOOK_CLASSIC_TARGET} PRIVATE
  LokaAppleToolboxCore
)

target_compile_definitions(${SCRAPBOOK_CLASSIC_TARGET} PRIVATE
  LOKA_RETRO68=1
)

set_target_properties(${SCRAPBOOK_CLASSIC_TARGET} PROPERTIES
  CXX_STANDARD 98
  CXX_STANDARD_REQUIRED YES
)

target_compile_options(${SCRAPBOOK_CLASSIC_TARGET} PRIVATE
  -Os
  -ffunction-sections
  -fdata-sections
  -fno-exceptions
  -fno-rtti
)
target_link_options(${SCRAPBOOK_CLASSIC_TARGET} PRIVATE
  -Wl,--gc-sections
)

if(CMAKE_SYSTEM_NAME STREQUAL "Retro68" AND NOT LOKA_RETRO68_DIAGNOSTICS)
  target_link_options(${SCRAPBOOK_CLASSIC_TARGET} PRIVATE
    -Wl,--mac-strip-macsbug
  )
endif()

# Keep the committed package beside the APPL/dsk outputs for the development
# disk script. Packing remains an explicit host-side regeneration step.
configure_file(ASSETS.LRP ASSETS.LRP COPYONLY)
