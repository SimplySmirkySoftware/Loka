cmake_minimum_required(VERSION 3.15)
project(LokaHelloWorld LANGUAGES CXX)

if(NOT DEFINED LOKA_SHARED_TEST_SOURCES)
  include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/LokaTestSources.cmake)
endif()

if(APPLE)
  enable_language(OBJCXX)
  loka_suppress_arclite_link()
endif()

if(NOT WIN32 AND NOT APPLE AND NOT (CMAKE_SYSTEM_NAME MATCHES "^Retro(68|PPC|Carbon)$"))
  add_custom_target(LokaHelloWorld ALL
    COMMAND ${CMAKE_COMMAND} -E echo "LokaHelloWorld has no supported platform target here."
  )
  return()
endif()

if(WIN32)
  include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/LokaWin32VisualStyles.cmake)
  set(HELLOWORLD_SOURCES
    src/main.cpp
    src/MainNode.cpp
  )

  if(NOT TARGET LokaWin32Core)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../win32 ${CMAKE_CURRENT_BINARY_DIR}/win32)
  endif()

  add_executable(LokaHelloWin32 WIN32 ${HELLOWORLD_SOURCES})
  loka_enable_warnings(LokaHelloWin32)
  loka_enable_win32_visual_styles(LokaHelloWin32)

  target_include_directories(LokaHelloWin32 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../../win32/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tests
  )

  target_link_libraries(LokaHelloWin32 PRIVATE LokaWin32Core)

  # Use UTF-8 source/execution charset on MSVC to avoid codepage warnings
  if(MSVC)
    target_compile_options(LokaHelloWin32 PRIVATE /utf-8)
  endif()

  # The test binary compiles the common sources itself so TEST_BUILD applies
  # to them uniformly (mirrors the Linux LokaFlowDslTests target and the
  # macOS LokaTestsMacOS target). Linking the production LokaWin32Core
  # instead would mix TEST_BUILD and non-TEST_BUILD compilations of the same
  # classes (ODR/layout divergence). Win32String.cpp supplies the platform
  # string seam that GenericString.cpp compiles out on _WIN32.
  file(GLOB_RECURSE LOKA_WIN32_TEST_COMMON_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/*.cpp
  )
  # EXCLUDE_FROM_ALL: mirrors LokaTestsMacOS — built explicitly by the
  # win32-tests build preset, not by Build All / generated IDE projects.
  add_executable(LokaTestsWin32 EXCLUDE_FROM_ALL
    ${LOKA_SHARED_TEST_SOURCES}
    ${LOKA_WIN32_TEST_COMMON_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../win32/src/platform/Win32String.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../win32/src/platform/Win32FileIO.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../win32/src/platform/Win32AppLocation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../win32/src/platform/Win32DebugLog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../win32/src/platform/Win32Profiler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../win32/src/platform/Win32IdlePacer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../win32/src/platform/Win32DisplayScale.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../win32/src/platform/Win32DisplayFont.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../win32/src/Win32ScenePlatformController.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../win32/src/Win32BuiltInSupport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../win32/src/Win32App.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../win32/src/Win32MenuAttachment.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../win32/src/Win32Window.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../win32/src/context/Win32RetirableContext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../win32/src/Win32PlatformContext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../win32/src/Win32BitmapCapture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../win32/src/context/Win32ButtonContext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../win32/src/context/Win32RibbonGroupEdgeContext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../win32/src/context/Win32CellContext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../win32/src/context/Win32TextContext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../win32/src/context/Win32EditTextContext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../win32/src/context/Win32TextEditorContext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../win32/src/context/Win32OpenFileDialogContext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../win32/src/context/Win32ConfirmDialogContext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../win32/src/context/Win32PopupMenuContext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../win32/src/context/Win32ImageViewContext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../win32/src/context/Win32RectSurfaceContext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../win32/src/context/Win32AttributedTextContext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../win32/src/context/Win32AttributedTextTable.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../win32/src/context/Win32ScrollViewContext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/Win32NativeResourceRetirementTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/Win32CardPainterTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/Win32NodeHandlerEnsureTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/Win32LiveResizeTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/Win32InputDoorTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/Win32ScrollViewTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/Win32ThreadModalScopeTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/Win32EditTextBridgeTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/Win32FocusTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/Win32MenuAttachmentTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/Win32TextEditorTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/Win32OpenFileDialogTransportTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/Win32ConfirmDialogTransportTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/Win32FilePathTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/Win32AppLocationTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/Win32ImageBlobTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/Win32PropsReconciliationTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/Win32RectSurfacePaintTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/Win32RectSurfaceRedrawTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/Win32WideApiTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/Win32RailMetricsTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/Win32WindowClientSizeTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/Win32ProfilerTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/Win32IdlePacerTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/Win32ManifestCompatibilityTests.cpp
  )
  loka_enable_warnings(LokaTestsWin32)
  loka_enable_win32_visual_styles(LokaTestsWin32)

  target_include_directories(LokaTestsWin32 PRIVATE
    ${LOKA_SCRAPBOOK_R_HEADER_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tools
    ${CMAKE_CURRENT_SOURCE_DIR}/../../win32/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tests
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../Tutorial/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../MineSweeper/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../FloppyBird/src
  )

  target_compile_definitions(LokaTestsWin32 PRIVATE TEST_BUILD)
  target_link_libraries(LokaTestsWin32 PRIVATE LokaWin32WideApiContract ole32 windowscodecs)

  if(MSVC)
    # tests/FlowDslTests.cpp outgrew the default COFF section limit (C1128).
    target_compile_options(LokaTestsWin32 PRIVATE /utf-8 /bigobj)
  endif()

  add_executable(LokaWin32StandaloneAuditFixture EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/scripts/Win32StandaloneAuditFixture.cpp)
  loka_enable_warnings(LokaWin32StandaloneAuditFixture)
  target_link_libraries(LokaWin32StandaloneAuditFixture PRIVATE
    LokaWin32WideApiContract)
  set_target_properties(LokaWin32StandaloneAuditFixture PROPERTIES
    CXX_STANDARD 98
    CXX_STANDARD_REQUIRED YES
  )
  add_dependencies(LokaTestsWin32 LokaWin32StandaloneAuditFixture)

  add_test(NAME LokaTestsWin32 COMMAND LokaTestsWin32)
  if(MSVC AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_test(NAME scriptWin32AssertFailure
      COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File
          ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/scripts/Win32AssertFailureTest.ps1
        -TestExecutable $<TARGET_FILE:LokaTestsWin32>)
    set_tests_properties(scriptWin32AssertFailure PROPERTIES TIMEOUT 45)
  endif()
  add_test(NAME scriptWin32StandaloneAuditVerifier
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
      -File
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/scripts/Win32StandaloneAuditVerifierTest.ps1
      -FixtureExecutable $<TARGET_FILE:LokaWin32StandaloneAuditFixture>)
  add_test(NAME scriptWin32ScenarioDpiAwareness
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
      -File
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/scripts/Win32ScenarioDpiAwarenessTest.ps1
      -Source
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/win32/ScenarioCaptureNative.cs
      -TestSource
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/win32/ScenarioCaptureNativeTests.cs)

endif()

if(APPLE)
  set(MACOS_SOURCES
    src/MacOSMain.mm
    src/MainNode.cpp
  )
  file(GLOB_RECURSE MACOS_HEADERS src/*.hpp)

  add_executable(LokaHelloMacOS ${MACOS_SOURCES} ${MACOS_HEADERS})
  loka_enable_warnings(LokaHelloMacOS)

  target_include_directories(LokaHelloMacOS PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apple/macos/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apple/macos/src/context
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tests
  )

  target_link_libraries(LokaHelloMacOS PRIVATE LokaMacOSCore)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND LOKA_MACOS_EXPLICIT_NO_ARC_FLAGS)
    target_compile_options(LokaHelloMacOS PRIVATE "-fno-objc-arc")
  endif()
endif()

if(CMAKE_SYSTEM_NAME MATCHES "^Retro(68|PPC|Carbon)$")
  set(LOKA_HELLO_CLASSIC_TARGET "LokaHello${LOKA_CLASSIC_TARGET_SUFFIX}")
  set(RETRO_SOURCES
    src/main.cpp
    src/MainNode.cpp
    Size.r
  )

  add_application(${LOKA_HELLO_CLASSIC_TARGET} ${RETRO_SOURCES})
  loka_enable_warnings(${LOKA_HELLO_CLASSIC_TARGET})

  target_include_directories(${LOKA_HELLO_CLASSIC_TARGET} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
  )

  target_link_libraries(${LOKA_HELLO_CLASSIC_TARGET} PRIVATE LokaAppleToolboxCore)

  target_compile_definitions(${LOKA_HELLO_CLASSIC_TARGET} PRIVATE LOKA_RETRO68=1)

  set_target_properties(${LOKA_HELLO_CLASSIC_TARGET} PROPERTIES
    CXX_STANDARD 98
    CXX_STANDARD_REQUIRED YES
  )

  target_compile_options(${LOKA_HELLO_CLASSIC_TARGET} PRIVATE
    -Os
    -ffunction-sections
    -fdata-sections
    -fno-exceptions
    -fno-rtti
  )
  target_link_options(${LOKA_HELLO_CLASSIC_TARGET} PRIVATE
    -Wl,--gc-sections
  )

  # Compact shipping profile (#135): drop the MacsBug name sections from the
  # CODE resources. Elf2Mac-only (68K); the DWARF .gdb ELF keeps full symbols
  # for the MAME gdbstub workflow.
  if(CMAKE_SYSTEM_NAME STREQUAL "Retro68" AND NOT LOKA_RETRO68_DIAGNOSTICS)
    target_link_options(${LOKA_HELLO_CLASSIC_TARGET} PRIVATE -Wl,--mac-strip-macsbug)
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
      add_custom_command(TARGET ${LOKA_HELLO_CLASSIC_TARGET} POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
          -DSTRIP_TOOL="${RETRO68_STRIP}"
          -DSTRIP_INPUT="$<TARGET_FILE:${LOKA_HELLO_CLASSIC_TARGET}>.gdb"
          -P "${CMAKE_SOURCE_DIR}/cmake/strip_classic.cmake"
        COMMENT "Stripping Classic debug binary"
      )
    endif()
  endif()
endif()
