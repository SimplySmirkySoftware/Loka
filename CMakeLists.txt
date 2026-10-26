cmake_minimum_required(VERSION 3.15)
project(Loka VERSION 0.0.8 LANGUAGES CXX)

option(LOKA_ENABLE_APPLE "Enable Apple platform targets" ON)
option(LOKA_BUILD_SMIRKYCARD "Build the optional QuickJS Scene-switching experiment" OFF)
option(LOKA_MACOS_EXPLICIT_NO_ARC_FLAGS "Add explicit -fno-objc-arc flags to macOS example targets" ON)
option(LOKA_MACOS_SUPPRESS_ARCLITE_LINK "Remove libarclite from generated macOS link settings" OFF)
option(TEST_BUILD "Enable test build mode" OFF)
option(LOKA_COMPILE_PINS "Register compile-refusal tests (Linux CI gate)" OFF)
option(LOKA_MAME_SCENARIOS "Register the MAME-backed scenario tests" OFF)
option(LOKA_WIN32_SCENARIOS "Register the rig-local Win32 scenario tests" OFF)
option(LOKA_ENABLE_STANDALONE_TARGETS
  "Register the finite Flow and autonomous Loop application targets" OFF)
option(LOKA_LIFECYCLE_AUDIT "Enable lifecycle alive-counter auditing" OFF)
option(LOKA_RETRO68_DIAGNOSTICS
  "Diagnostic Retro68 profile: keep MacsBug names, the function-tick profiler, and debug-stats dumps" OFF)
set(LOKA_SCRAPBOOK_R_HEADER_DIR
  "${PROJECT_SOURCE_DIR}/example/ScrapbookUI/src" CACHE PATH
  "Directory containing lrpc's generated ScrapbookUI R.hpp")
option(LOKA_TOOLBOX_MULTIVERSAL_INTERFACES
  "Build Toolbox targets against Retro68's redistributable Multiversal Interfaces" OFF)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/LokaWarnings.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/LokaStandalonePerformance.cmake)

macro(loka_suppress_arclite_link)
  if(LOKA_MACOS_SUPPRESS_ARCLITE_LINK)
    foreach(_loka_implicit_link_var
        CMAKE_CXX_IMPLICIT_LINK_LIBRARIES
        CMAKE_OBJCXX_IMPLICIT_LINK_LIBRARIES)
      if(DEFINED ${_loka_implicit_link_var})
        list(FILTER ${_loka_implicit_link_var} EXCLUDE REGEX ".*arclite.*")
      endif()
    endforeach()
  endif()
endmacro()

set(LOKA_CLASSIC_MAC OFF)
if(CMAKE_SYSTEM_NAME MATCHES "^Retro(68|PPC|Carbon)$")
  set(LOKA_CLASSIC_MAC ON)
endif()

set(LOKA_MACOS OFF)
if(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  set(LOKA_MACOS ON)
  # CMake looks up its default bundle template through CMAKE_MODULE_PATH.
  # Keep Retina capability explicit for old-SDK builds and future app bundles.
  list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/macos")
endif()

# Compact vs diagnostic Classic profile (#135). Compact (the default) ships
# without the function-tick profiler and without the debug-stats dump chain;
# the diagnostic profile keeps both plus MacsBug names. add_definitions so
# the macros stay uniform across LokaAppleToolboxCore and the app targets
# (PROFILE_FUNC expands inside shared headers).
if(LOKA_CLASSIC_MAC AND LOKA_RETRO68_DIAGNOSTICS)
  add_definitions(-DLOKA_RETRO68_DIAGNOSTICS=1 -DLOKA_PROFILE_FUNC_TICKS=1)
endif()

set(LOKA_CLASSIC_TARGET_SUFFIX "")
if(CMAKE_SYSTEM_NAME STREQUAL "Retro68")
  set(LOKA_CLASSIC_TARGET_SUFFIX "68K")
elseif(CMAKE_SYSTEM_NAME MATCHES "^Retro(PPC|Carbon)$")
  set(LOKA_CLASSIC_TARGET_SUFFIX "PPC")
endif()

# Size.r files keep the 68K partitions (#1103) apart from the PPC ones (#1109)
# through this generated Rez include.
if(LOKA_CLASSIC_MAC)
  set(LOKA_CLASSIC_68K 0)
  if(CMAKE_SYSTEM_NAME STREQUAL "Retro68")
    set(LOKA_CLASSIC_68K 1)
  endif()
  configure_file(cmake/LokaClassicTarget.r.in "${CMAKE_BINARY_DIR}/rez/LokaClassicTarget.r" @ONLY)
  list(APPEND REZ_INCLUDE_PATH "${CMAKE_BINARY_DIR}/rez")
endif()

if(TEST_BUILD)
  add_definitions(-DTEST_BUILD -DLOKA_DIAG=1)
endif()

if(LOKA_LIFECYCLE_AUDIT)
  add_definitions(-DLOKA_LIFECYCLE_AUDIT)
endif()

# Top-level CTest wiring so `ctest --test-dir <build dir>` finds test targets
# registered in subdirectories (e.g. LokaTestsMacOS) as well as the headless
# suite below. include(CTest) rather than enable_testing() so ctest's
# DartConfiguration.tcl exists (VS Code invokes ctest with -T test). The
# property suppresses the CDash dashboard targets (Nightly/Continuous/...)
# that would otherwise clutter generated Xcode/MSVC projects.
set_property(GLOBAL PROPERTY CTEST_TARGETS_ADDED 1)
include(CTest)

if(NOT WIN32 AND NOT LOKA_CLASSIC_MAC)
  add_test(NAME scriptClassicReleaseDisks
    COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/scripts/ClassicReleaseDisksTest.py)
  add_test(NAME scriptMameDevDisk
    COMMAND bash ${PROJECT_SOURCE_DIR}/tests/scripts/MameDevDiskTest.sh)
  add_test(NAME scriptMameBootDisk
    COMMAND bash ${PROJECT_SOURCE_DIR}/tests/scripts/MameBootDiskTest.sh)
endif()

foreach(_loka_invalid_performance_runs IN ITEMS abc ON 0 2 11)
  add_test(NAME "standalonePerformanceConfigRejects-${_loka_invalid_performance_runs}"
    COMMAND ${CMAKE_COMMAND}
      -DLOKA_REPOSITORY_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
      -DLOKA_TEST_PERFORMANCE_RUNS=${_loka_invalid_performance_runs}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts/StandalonePerformanceConfigProbe.cmake)
  set_tests_properties(
    "standalonePerformanceConfigRejects-${_loka_invalid_performance_runs}"
    PROPERTIES WILL_FAIL TRUE)
endforeach()
unset(_loka_invalid_performance_runs)
add_test(NAME standalonePerformanceConfigAccepts-3
  COMMAND ${CMAKE_COMMAND}
    -DLOKA_REPOSITORY_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
    -DLOKA_TEST_PERFORMANCE_RUNS=3
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts/StandalonePerformanceConfigProbe.cmake)

# The SmirkyCard disk-script ASCII wall, one case per refusal shape: an
# embedded NUL followed by an em-dash (a text read stops at the NUL), a bare
# em-dash, a control byte, DEL, and a "00" that straddles two allowed bytes.
foreach(_loka_ascii_case IN ITEMS
    "clean|6f6b09746162200d0a|-1"
    "nulThenEmDash|61626300646566e2809478|3"
    "emDash|616263e2809478|3"
    "control|610162|1"
    "del|617f|1"
    "straddledZeroes|200a300a|-1")
  string(REPLACE "|" ";" _loka_ascii_fields "${_loka_ascii_case}")
  list(GET _loka_ascii_fields 0 _loka_ascii_name)
  list(GET _loka_ascii_fields 1 _loka_ascii_hex)
  list(GET _loka_ascii_fields 2 _loka_ascii_offset)
  add_test(NAME "smirkyCardDiskScriptAscii-${_loka_ascii_name}"
    COMMAND ${CMAKE_COMMAND}
      -DLOKA_REPOSITORY_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
      -DHEX=${_loka_ascii_hex}
      -DEXPECT_OFFSET=${_loka_ascii_offset}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts/SmirkyCardDiskScriptAsciiProbe.cmake)
endforeach()
unset(_loka_ascii_case)
unset(_loka_ascii_fields)
unset(_loka_ascii_name)
unset(_loka_ascii_hex)
unset(_loka_ascii_offset)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/LokaTestSources.cmake)

# Compile-only regression pins are built by the headless test profile and by
# the Classic diagnostic profile, but are never linked into shipped binaries.
if(TEST_BUILD OR (LOKA_CLASSIC_MAC AND LOKA_RETRO68_DIAGNOSTICS))
  add_library(LokaProfilerCompileTest OBJECT
    ${PROJECT_SOURCE_DIR}/tests/ProfilerCompileTest.cpp
  )
  loka_enable_warnings(LokaProfilerCompileTest)
  target_include_directories(LokaProfilerCompileTest PRIVATE
    ${PROJECT_SOURCE_DIR}/common
  )
  target_compile_definitions(LokaProfilerCompileTest PRIVATE
    LOKA_PROFILE_FUNC_TICKS=1
  )
  set_target_properties(LokaProfilerCompileTest PROPERTIES
    CXX_STANDARD 98
    CXX_STANDARD_REQUIRED YES
  )
endif()

if(WIN32)
  add_subdirectory(win32)
endif()

if(APPLE AND LOKA_ENABLE_APPLE)
  add_subdirectory(apple)
endif()

if(LOKA_CLASSIC_MAC)
  add_subdirectory(apple/common/carbon)
  add_subdirectory(apple/toolbox)
endif()

if(WIN32 OR (LOKA_MACOS AND LOKA_ENABLE_APPLE) OR LOKA_CLASSIC_MAC)
add_subdirectory(example/HelloWorld)
add_subdirectory(example/MineSweeper)
add_subdirectory(example/SimpleViewer)
add_subdirectory(example/SimpleText)
add_subdirectory(example/ScrapbookUI)
add_subdirectory(example/FloppyBird)
add_subdirectory(example/SmirkBench)
add_subdirectory(example/LazyList)
add_subdirectory(example/Tutorial)
add_subdirectory(example/CardShowcase)

if(WIN32 AND TARGET LokaStandaloneLoopWin32All)
  add_dependencies(LokaStandaloneLoopWin32All LokaSimpleViewerWin32 LokaSimpleTextWin32)
endif()
if(APPLE AND TARGET LokaStandaloneLoopMacOSAll)
  add_dependencies(LokaStandaloneLoopMacOSAll LokaSimpleViewerMacOS)
endif()
endif()

if(NOT IOS)
  add_subdirectory(tests/toolbox)
endif()

# Shared compile environment for contract tests and their compile-only twins.
add_library(LokaContractTestEnvironment INTERFACE)
target_include_directories(LokaContractTestEnvironment INTERFACE
  ${LOKA_SCRAPBOOK_R_HEADER_DIR}
  ${PROJECT_SOURCE_DIR}/tests
  ${PROJECT_SOURCE_DIR}/tools
  ${PROJECT_SOURCE_DIR}/common
)
target_compile_definitions(LokaContractTestEnvironment INTERFACE TEST_BUILD)

if(TEST_BUILD)
  add_subdirectory(tests/compile)
endif()

# Linux/WSL headless tests.
if(NOT WIN32 AND NOT APPLE AND NOT LOKA_CLASSIC_MAC)
  file(GLOB_RECURSE LOKA_COMMON_SOURCES CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/common/*.cpp
  )

  # The shipping compile. Every Linux target above defines TEST_BUILD, so the
  # branches that only exist without it -- MenuBarDefinition::clone()'s
  # failure-injection gate, the TEST_ID paths -- were never compiled on this
  # platform at all. This object library compiles common/ the way a shipped
  # binary sees it, under the same warnings-as-errors floor. It is never
  # linked; the point is that the code is seen by a compiler.
  add_library(LokaCommonShippingCompileTest OBJECT
    ${LOKA_COMMON_SOURCES}
  )
  loka_enable_warnings(LokaCommonShippingCompileTest)
  # add_definitions(-DTEST_BUILD) is directory-scoped, so this target has to
  # take the define back off to be the shipping compile it claims to be.
  target_compile_options(LokaCommonShippingCompileTest PRIVATE
    -UTEST_BUILD
    -ULOKA_DIAG
  )
  target_include_directories(LokaCommonShippingCompileTest PRIVATE
    ${PROJECT_SOURCE_DIR}/common
    ${PROJECT_SOURCE_DIR}/tools
  )
  set_target_properties(LokaCommonShippingCompileTest PROPERTIES
    CXX_STANDARD 98
    CXX_STANDARD_REQUIRED YES
  )

  add_library(LokaCommonTesting STATIC
    ${LOKA_COMMON_SOURCES}
  )
  loka_enable_warnings(LokaCommonTesting)
  target_link_libraries(LokaCommonTesting PUBLIC LokaContractTestEnvironment)
  set_target_properties(LokaCommonTesting PROPERTIES
    CXX_STANDARD 98
    CXX_STANDARD_REQUIRED YES
  )

  set(LOKA_CONTRACT_TEST_SOURCES ${LOKA_SHARED_TEST_SOURCES})
  list(FILTER LOKA_CONTRACT_TEST_SOURCES EXCLUDE REGEX "/tests/FlowDslTests\\.cpp$")
  list(FILTER LOKA_CONTRACT_TEST_SOURCES EXCLUDE REGEX "/tests/OwnershipDumpTests\\.cpp$")
  list(FILTER LOKA_CONTRACT_TEST_SOURCES EXCLUDE REGEX "/tests/FlowDslTestMain\\.cpp$")
  list(FILTER LOKA_CONTRACT_TEST_SOURCES EXCLUDE REGEX "/tests/HelloWorldScenarioTests\\.cpp$")
  list(FILTER LOKA_CONTRACT_TEST_SOURCES EXCLUDE REGEX "/tests/scenarios/HelloWorldScenarios\\.cpp$")
  list(FILTER LOKA_CONTRACT_TEST_SOURCES EXCLUDE REGEX "/tests/standalone/HelloWorldStandaloneFlowAppConfig\\.cpp$")
  list(FILTER LOKA_CONTRACT_TEST_SOURCES EXCLUDE REGEX "/tests/TutorialScenarioTests\\.cpp$")
  list(FILTER LOKA_CONTRACT_TEST_SOURCES EXCLUDE REGEX "/tests/scenarios/TutorialScenarios\\.cpp$")
  list(FILTER LOKA_CONTRACT_TEST_SOURCES EXCLUDE REGEX "/tests/standalone/TutorialStandaloneFlowAppConfig\\.cpp$")
  list(FILTER LOKA_CONTRACT_TEST_SOURCES EXCLUDE REGEX "/tests/MineSweeperScenarioTests\\.cpp$")
  list(FILTER LOKA_CONTRACT_TEST_SOURCES EXCLUDE REGEX "/tests/scenarios/MineSweeperScenarios\\.cpp$")
  list(FILTER LOKA_CONTRACT_TEST_SOURCES EXCLUDE REGEX "/tests/standalone/MineSweeperStandaloneFlowAppConfig\\.cpp$")
  list(FILTER LOKA_CONTRACT_TEST_SOURCES EXCLUDE REGEX "/tests/FloppyBirdScenarioTests\\.cpp$")
  list(FILTER LOKA_CONTRACT_TEST_SOURCES EXCLUDE REGEX "/tests/scenarios/FloppyBirdScenarios\\.cpp$")
  list(FILTER LOKA_CONTRACT_TEST_SOURCES EXCLUDE REGEX "/tests/standalone/FloppyBirdStandaloneFlowAppConfig\\.cpp$")
  list(FILTER LOKA_CONTRACT_TEST_SOURCES EXCLUDE REGEX "/tests/StartupScenarioTests\\.cpp$")
  list(FILTER LOKA_CONTRACT_TEST_SOURCES EXCLUDE REGEX "/tests/scenarios/StartupScenarios\\.cpp$")
  list(FILTER LOKA_CONTRACT_TEST_SOURCES EXCLUDE REGEX "/tests/ScenarioReelTests\\.cpp$")
  list(FILTER LOKA_CONTRACT_TEST_SOURCES EXCLUDE REGEX "/tests/StandalonePerformanceTests\\.cpp$")
  list(FILTER LOKA_CONTRACT_TEST_SOURCES EXCLUDE REGEX "/tests/standalone/StandalonePerformance\\.cpp$")
  list(FILTER LOKA_CONTRACT_TEST_SOURCES EXCLUDE REGEX "/tests/scenarios/RectSurfaceScenarioObservation\\.cpp$")

  add_executable(LokaContractTests
    ${PROJECT_SOURCE_DIR}/tests/ContractTestMain.cpp
    ${LOKA_CONTRACT_TEST_SOURCES}
  )
  loka_enable_warnings(LokaContractTests)
  set_target_properties(LokaContractTests PROPERTIES
    CXX_STANDARD 98
    CXX_STANDARD_REQUIRED YES
  )
  target_link_libraries(LokaContractTests PRIVATE LokaCommonTesting)

  add_executable(LokaScenarioTests
    ${PROJECT_SOURCE_DIR}/tests/ScenarioTestMain.cpp
    ${PROJECT_SOURCE_DIR}/tests/TestingHooks.cpp
    ${PROJECT_SOURCE_DIR}/tests/FlowDslTests.cpp
    ${PROJECT_SOURCE_DIR}/tests/OwnershipDumpTests.cpp
    ${PROJECT_SOURCE_DIR}/tests/HelloWorldScenarioTests.cpp
    ${PROJECT_SOURCE_DIR}/tests/ScenarioReelTests.cpp
    ${PROJECT_SOURCE_DIR}/tests/StandalonePerformanceTests.cpp
    ${PROJECT_SOURCE_DIR}/tests/scenarios/ScenarioTypes.cpp
    ${PROJECT_SOURCE_DIR}/tests/standalone/StandalonePerformance.cpp
    ${PROJECT_SOURCE_DIR}/tests/scenarios/ScenarioReel.cpp
    ${PROJECT_SOURCE_DIR}/tests/scenarios/HelloWorldScenarios.cpp
    ${PROJECT_SOURCE_DIR}/tests/TutorialScenarioTests.cpp
    ${PROJECT_SOURCE_DIR}/tests/scenarios/TutorialScenarios.cpp
    ${PROJECT_SOURCE_DIR}/tests/standalone/TutorialStandaloneFlowAppConfig.cpp
    ${PROJECT_SOURCE_DIR}/tests/MineSweeperScenarioTests.cpp
    ${PROJECT_SOURCE_DIR}/tests/scenarios/MineSweeperScenarios.cpp
    ${PROJECT_SOURCE_DIR}/tests/standalone/MineSweeperStandaloneFlowAppConfig.cpp
    ${PROJECT_SOURCE_DIR}/tests/FloppyBirdScenarioTests.cpp
    ${PROJECT_SOURCE_DIR}/tests/scenarios/FloppyBirdScenarios.cpp
    ${PROJECT_SOURCE_DIR}/tests/scenarios/RectSurfaceScenarioObservation.cpp
    ${PROJECT_SOURCE_DIR}/tests/standalone/FloppyBirdStandaloneFlowAppConfig.cpp
    ${PROJECT_SOURCE_DIR}/tests/StartupScenarioTests.cpp
    ${PROJECT_SOURCE_DIR}/tests/scenarios/StartupScenarios.cpp
    ${PROJECT_SOURCE_DIR}/tests/platform/null/NullScenePlatformController.cpp
    ${PROJECT_SOURCE_DIR}/tests/platform/null/NullPlatformContext.cpp
    ${PROJECT_SOURCE_DIR}/tests/platform/null/context/NullButtonContext.cpp
    ${PROJECT_SOURCE_DIR}/tests/platform/null/context/NullEditTextContext.cpp
    ${PROJECT_SOURCE_DIR}/tests/platform/null/context/NullTextEditorContext.cpp
    ${PROJECT_SOURCE_DIR}/tests/platform/null/context/NullScrollBarContext.cpp
    ${PROJECT_SOURCE_DIR}/tests/platform/null/context/NullTextMetrics.cpp
    ${PROJECT_SOURCE_DIR}/tests/platform/null/context/NullAttributedTextContext.cpp
    ${PROJECT_SOURCE_DIR}/tests/platform/null/context/NullTextContext.cpp
    ${PROJECT_SOURCE_DIR}/tests/platform/null/context/NullRectSurfaceContext.cpp
    ${PROJECT_SOURCE_DIR}/tests/standalone/HelloWorldStandaloneFlowAppConfig.cpp
    ${PROJECT_SOURCE_DIR}/tests/standalone/StandaloneScenarioSupport.cpp
    ${PROJECT_SOURCE_DIR}/example/HelloWorld/src/MainNode.cpp
  )
  loka_enable_warnings(LokaScenarioTests)
  target_include_directories(LokaScenarioTests PRIVATE
    ${PROJECT_SOURCE_DIR}/tests
    ${PROJECT_SOURCE_DIR}/example/SimpleViewer/src
    ${PROJECT_SOURCE_DIR}/example/HelloWorld/src
    ${PROJECT_SOURCE_DIR}/example/Tutorial/src
    ${PROJECT_SOURCE_DIR}/example/MineSweeper/src
    ${PROJECT_SOURCE_DIR}/example/FloppyBird/src
    ${PROJECT_SOURCE_DIR}/tools
  )
  set_target_properties(LokaScenarioTests PROPERTIES
    CXX_STANDARD 98
    CXX_STANDARD_REQUIRED YES
  )
  target_compile_definitions(LokaScenarioTests PRIVATE TEST_BUILD)
  target_link_libraries(LokaScenarioTests PRIVATE LokaCommonTesting)
  # Exercise nullable clone allocations without adding a production allocation door.
  target_compile_definitions(LokaContractTests PRIVATE LOKA_CARD_CLONE_FAULT_PINS)
  target_compile_options(LokaContractTests PRIVATE -fcheck-new)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    target_link_options(LokaContractTests PRIVATE -Wl,--wrap=_Znwm)
  else()
    target_link_options(LokaContractTests PRIVATE -Wl,--wrap=_Znwj)
  endif()
  # GNU LP64 member ABI: intercept bag entry without adding a production test door.
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    target_compile_definitions(LokaContractTests PRIVATE LOKA_BAG_READ_FAULT_PINS)
    target_link_options(LokaContractTests PRIVATE
      -Wl,--wrap=_ZN4loka4core8resource4lrpk6Reader11readBagIntoEmPhm)
  endif()
  # Characterize the real adapter/seam with deterministic stdio failures.
  # Wrapping is confined to this Linux test executable, never a core library.
  target_compile_definitions(LokaScenarioTests PRIVATE LOKA_FILE_READ_FAULT_PINS)
  target_link_options(LokaScenarioTests PRIVATE
    -Wl,--wrap=fseek -Wl,--wrap=ftell -Wl,--wrap=fread -Wl,--wrap=ferror -Wl,--wrap=fclose)


  function(loka_add_vehicle_presentation_test target_name source_file example_include_dir test_name)
    add_executable(${target_name}
      ${PROJECT_SOURCE_DIR}/tests/scenarios/${source_file}
      ${PROJECT_SOURCE_DIR}/tests/TestingHooks.cpp
      ${PROJECT_SOURCE_DIR}/tests/platform/null/NullPlatformContext.cpp
      ${PROJECT_SOURCE_DIR}/tests/platform/null/NullScenePlatformController.cpp
      ${PROJECT_SOURCE_DIR}/tests/platform/null/context/NullButtonContext.cpp
      ${PROJECT_SOURCE_DIR}/tests/platform/null/context/NullEditTextContext.cpp
      ${PROJECT_SOURCE_DIR}/tests/platform/null/context/NullTextEditorContext.cpp
      ${PROJECT_SOURCE_DIR}/tests/platform/null/context/NullScrollBarContext.cpp
      ${PROJECT_SOURCE_DIR}/tests/platform/null/context/NullTextMetrics.cpp
      ${PROJECT_SOURCE_DIR}/tests/platform/null/context/NullAttributedTextContext.cpp
      ${PROJECT_SOURCE_DIR}/tests/platform/null/context/NullTextContext.cpp
      ${PROJECT_SOURCE_DIR}/tests/platform/null/context/NullRectSurfaceContext.cpp
    )
    loka_enable_warnings(${target_name})
    target_include_directories(${target_name} PRIVATE
      ${PROJECT_SOURCE_DIR}/common
      ${PROJECT_SOURCE_DIR}/tests
      ${PROJECT_SOURCE_DIR}/${example_include_dir}
    )
    set_target_properties(${target_name} PROPERTIES
      CXX_STANDARD 98
      CXX_STANDARD_REQUIRED YES
    )
    target_compile_definitions(${target_name} PRIVATE TEST_BUILD)
    target_link_libraries(${target_name} PRIVATE LokaCommonTesting)
    add_test(NAME ${test_name} COMMAND $<TARGET_FILE:${target_name}>)
  endfunction()

  loka_add_vehicle_presentation_test(
    LokaTutorialVehiclePresentationTest
    TutorialVehiclePresentationTest.cpp
    example/Tutorial/src
    testTutorialVehiclePresentationUsesExampleDeclaration)
  loka_add_vehicle_presentation_test(
    LokaMineSweeperVehiclePresentationTest
    MineSweeperVehiclePresentationTest.cpp
    example/MineSweeper/src
    testMineSweeperVehiclePresentationUsesExampleDeclaration)
  loka_add_vehicle_presentation_test(
    LokaFloppyBirdVehiclePresentationTest
    FloppyBirdVehiclePresentationTest.cpp
    example/FloppyBird/src
    testFloppyBirdVehiclePresentationUsesExampleDeclaration)
  loka_add_vehicle_presentation_test(
    LokaScrapbookVehiclePresentationTest
    ScrapbookVehiclePresentationTest.cpp
    example/ScrapbookUI/src
    testScrapbookVehiclePresentationUsesExampleDeclaration)
  target_sources(LokaScrapbookVehiclePresentationTest PRIVATE
    ${PROJECT_SOURCE_DIR}/example/ScrapbookUI/src/ScrapbookPackage.cpp
  )
  target_include_directories(LokaScrapbookVehiclePresentationTest BEFORE PRIVATE
    ${LOKA_SCRAPBOOK_R_HEADER_DIR}
  )

  # Allocation ratchet (tests/alloc-ratchet, grown from probe/zero-alloc-pin).
  # Its own binary because it replaces the global operator new/delete to
  # census heap allocations; that replacement must not perturb the other
  # test executables. Asserts steady-state per-interaction allocations stay
  # under a fixed budget (a ceiling, not a target); prints the per-stack
  # census only when the ratchet trips.
  # Reuse the shared Null rail inventory for the surface allocation fixture.
  set(LOKA_ALLOC_PIN_NULL_SOURCES ${LOKA_SHARED_TEST_SOURCES})
  list(FILTER LOKA_ALLOC_PIN_NULL_SOURCES INCLUDE REGEX "/tests/platform/null/.*\\.cpp$")
  add_executable(LokaAllocPinTests
    ${LOKA_ALLOC_PIN_NULL_SOURCES}
    ${PROJECT_SOURCE_DIR}/tests/TestingHooks.cpp
    ${PROJECT_SOURCE_DIR}/tests/AllocPinMain.cpp
    ${PROJECT_SOURCE_DIR}/tests/AllocPinTests.cpp
    ${PROJECT_SOURCE_DIR}/tests/LazyViewTests.cpp
    ${PROJECT_SOURCE_DIR}/tests/SmallObjectPoolTests.cpp
    ${PROJECT_SOURCE_DIR}/tests/StateTrackerAllocationTests.cpp
    ${PROJECT_SOURCE_DIR}/example/HelloWorld/src/MainNode.cpp
  )
  loka_enable_warnings(LokaAllocPinTests)
  target_include_directories(LokaAllocPinTests PRIVATE
    ${PROJECT_SOURCE_DIR}/tests
    ${PROJECT_SOURCE_DIR}/example/HelloWorld/src
    ${PROJECT_SOURCE_DIR}/tools
  )
  set_target_properties(LokaAllocPinTests PROPERTIES
    CXX_STANDARD 98
    CXX_STANDARD_REQUIRED YES
  )
  target_compile_definitions(LokaAllocPinTests PRIVATE TEST_BUILD LOKA_STATE_TRACKER_ALLOC_CENSUS LOKA_LAZYFLEX_ALLOC_CENSUS)
  target_link_libraries(LokaAllocPinTests PRIVATE LokaCommonTesting)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # Export symbols so backtrace_symbols can attribute census stacks.
    target_link_options(LokaAllocPinTests PRIVATE -rdynamic)
  endif()

  # Separate host simulation of Classic pool routing; original heap ratchets stay intact.
  get_target_property(LOKA_UPSTREAM_PIN_SOURCES LokaAllocPinTests SOURCES)
  add_executable(LokaUpstreamGaugePins ${LOKA_UPSTREAM_PIN_SOURCES}
    ${PROJECT_SOURCE_DIR}/tests/SeatReservationTests.cpp)
  loka_enable_warnings(LokaUpstreamGaugePins)
  target_include_directories(LokaUpstreamGaugePins PRIVATE tests example/HelloWorld/src tools)
  set_target_properties(LokaUpstreamGaugePins PROPERTIES CXX_STANDARD 98 CXX_STANDARD_REQUIRED YES)
  target_compile_definitions(LokaUpstreamGaugePins PRIVATE TEST_BUILD
    LOKA_STATE_TRACKER_ALLOC_CENSUS LOKA_LAZYFLEX_ALLOC_CENSUS LOKA_UPSTREAM_GAUGE_PIN)
  target_link_libraries(LokaUpstreamGaugePins PRIVATE LokaCommonTesting)
  add_test(NAME LokaUpstreamGaugePins COMMAND LokaUpstreamGaugePins)

  add_executable(LokaToolboxUnicodeStartupTests
    tests/toolbox/host/ToolboxUnicodeStartupHostTests.cpp
    apple/toolbox/src/ToolboxUnicodeStartup.cpp)
  target_include_directories(LokaToolboxUnicodeStartupTests PRIVATE
    tests/toolbox/host/startup tests/toolbox/host/oom apple/toolbox/src tests)
  target_compile_definitions(LokaToolboxUnicodeStartupTests PRIVATE LOKA_TOOLBOX_UNICODE=1)
  target_compile_options(LokaToolboxUnicodeStartupTests PRIVATE -fno-exceptions -fno-rtti)
  loka_enable_warnings(LokaToolboxUnicodeStartupTests)
  set_target_properties(LokaToolboxUnicodeStartupTests PROPERTIES
    CXX_STANDARD 98 CXX_STANDARD_REQUIRED YES)
  add_test(NAME LokaToolboxUnicodeStartupTests COMMAND LokaToolboxUnicodeStartupTests)

  add_executable(LokaToolboxOutOfMemoryTests
    tests/toolbox/host/ToolboxOutOfMemoryHostTests.cpp
    apple/toolbox/src/ToolboxOutOfMemory.cpp
    apple/toolbox/src/ToolboxDialogConstruction.cpp
    apple/toolbox/src/ToolboxReserveGrowZone.cpp)
  target_include_directories(LokaToolboxOutOfMemoryTests PRIVATE
    tests/toolbox/host/oom apple/toolbox/src tests)
  target_compile_options(LokaToolboxOutOfMemoryTests PRIVATE -fno-exceptions -fno-rtti)
  loka_enable_warnings(LokaToolboxOutOfMemoryTests)
  set_target_properties(LokaToolboxOutOfMemoryTests PROPERTIES
    CXX_STANDARD 98 CXX_STANDARD_REQUIRED YES)
  add_test(NAME LokaToolboxOutOfMemoryTests COMMAND LokaToolboxOutOfMemoryTests)
  add_test(NAME LokaToolboxOutOfMemoryWiring
    COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/toolbox/host/CheckOutOfMemoryWiring.py
      ${PROJECT_SOURCE_DIR})

  # Exercise the Classic source itself against a host Memory Manager substitute.
  add_executable(LokaToolboxMemorySourceTests tests/ToolboxMemorySourceTests.cpp
    apple/toolbox/src/ToolboxReserveGrowZone.cpp)
  target_link_libraries(LokaToolboxMemorySourceTests PRIVATE LokaCommonTesting)
  loka_enable_warnings(LokaToolboxMemorySourceTests)
  target_include_directories(LokaToolboxMemorySourceTests PRIVATE
    tests/support/classic-memory tests apple/toolbox/src common)
  set_target_properties(LokaToolboxMemorySourceTests PROPERTIES
    CXX_STANDARD 98 CXX_STANDARD_REQUIRED YES)
  add_test(NAME LokaToolboxMemorySourceTests COMMAND LokaToolboxMemorySourceTests)

  # Dedicated Classic file slice: never link common objects built without
  # LOKA_RETRO68, which changes File and FileHandle definitions.
  set(LOKA_TOOLBOX_FILE_COMMON_SOURCES ${LOKA_COMMON_SOURCES})
  list(FILTER LOKA_TOOLBOX_FILE_COMMON_SOURCES EXCLUDE REGEX "/platform/GenericString\\.cpp$")
  add_executable(LokaToolboxFileChoiceTests
    tests/toolbox/host/ToolboxFileChoiceHostTests.cpp
    tests/toolbox/host/ToolboxFileHost.cpp
    apple/toolbox/src/ToolboxFileChoice.cpp
    apple/toolbox/src/platform/ToolboxPascalText.cpp
    apple/toolbox/src/ToolboxByteSource.cpp
    apple/toolbox/src/ToolboxString.cpp
    apple/toolbox/src/platform/ToolboxAppLocation.cpp
    apple/toolbox/src/platform/ToolboxFileIO.cpp
    apple/toolbox/src/ToolboxBusy.cpp
    apple/toolbox/src/platform/ToolboxHfsName.cpp
    apple/toolbox/src/platform/ToolboxMacRoman.cpp
    ${LOKA_TOOLBOX_FILE_COMMON_SOURCES}
    tests/TestingHooks.cpp
    apple/toolbox/src/context/ToolboxOpenFileDialogContext.cpp
    apple/toolbox/src/context/ToolboxConfirmDialogContext.cpp
    apple/toolbox/src/ToolboxPendingDialogs.cpp
    apple/toolbox/src/ToolboxDialogConstruction.cpp)

  target_include_directories(LokaToolboxFileChoiceTests PRIVATE
    tests/toolbox/host/dialog tests/toolbox/host tests apple/toolbox/src common tools example/SimpleViewer/src)
  target_compile_definitions(LokaToolboxFileChoiceTests PRIVATE LOKA_RETRO68=1)
  target_compile_options(LokaToolboxFileChoiceTests PRIVATE -fno-exceptions -fno-rtti
    -include ${PROJECT_SOURCE_DIR}/tests/toolbox/host/ToolboxFileChoiceHost.hpp)
  loka_enable_warnings(LokaToolboxFileChoiceTests)
  set_target_properties(LokaToolboxFileChoiceTests PROPERTIES CXX_STANDARD 98 CXX_STANDARD_REQUIRED YES)
  foreach(_file_case tuple single application viewer smirky refused collision-ab collision-ba decoy canonical validation allocation display copies dialog validity busy prepare prepare-refuses-write save save-retarget present-render present-update present-detach present-destroy present-close present-background present-quit present-fifo present-two-windows present-modal-destroy-suppresses present-save present-reattach present-props present-lifecycle present-borrow present-controller present-no-group present-retire present-phase present-snapshot present-non-toolbox present-show present-scene-replace present-quit-delivery confirm-present-maps-items mixed-fifo-order confirm-newdialog-failure-seals-error close-during-pending-revokes-both-payloads confirm-lifecycle confirm-retarget confirm-modal-destroy-suppresses confirm-items-failure-seals-error confirm-retarget-channels confirm-seals-before-admission confirm-filter-failure-seals-error)
    add_test(NAME testToolboxFileChoice_${_file_case} COMMAND LokaToolboxFileChoiceTests ${_file_case})
  endforeach()

  # The sentinel teardown refusal must remain safe when lifecycle auditing is off.
  get_target_property(_dialog_sources LokaToolboxFileChoiceTests SOURCES)
  add_executable(LokaToolboxFileChoiceReleaseTests ${_dialog_sources})
  target_include_directories(LokaToolboxFileChoiceReleaseTests PRIVATE
    tests/toolbox/host/dialog tests/toolbox/host tests apple/toolbox/src common tools example/SimpleViewer/src)
  target_compile_definitions(LokaToolboxFileChoiceReleaseTests PRIVATE LOKA_RETRO68=1)
  target_compile_options(LokaToolboxFileChoiceReleaseTests PRIVATE -fno-exceptions -fno-rtti
    -ULOKA_LIFECYCLE_AUDIT
    -include ${PROJECT_SOURCE_DIR}/tests/toolbox/host/ToolboxFileChoiceHost.hpp)
  loka_enable_warnings(LokaToolboxFileChoiceReleaseTests)
  set_target_properties(LokaToolboxFileChoiceReleaseTests PROPERTIES CXX_STANDARD 98 CXX_STANDARD_REQUIRED YES)
  add_test(NAME testToolboxFileChoice_present-list-destroy
    COMMAND LokaToolboxFileChoiceReleaseTests present-list-destroy)

  add_executable(LokaToolboxBoxLayoutTests
    tests/toolbox/host/ToolboxBoxLayoutHostTests.cpp
    apple/toolbox/src/context/ToolboxClipBoxContext.cpp
    apple/toolbox/src/ToolboxNodeDispatch.cpp
    apple/toolbox/src/ToolboxPlatformLayoutHandlers.cpp tests/TestingHooks.cpp)
  target_include_directories(LokaToolboxBoxLayoutTests PRIVATE tests/toolbox/host tests apple/toolbox/src)
  target_compile_options(LokaToolboxBoxLayoutTests PRIVATE
    -include ${PROJECT_SOURCE_DIR}/tests/toolbox/host/ToolboxBoxLayoutHost.hpp)
  target_link_libraries(LokaToolboxBoxLayoutTests PRIVATE LokaCommonTesting)
  loka_enable_warnings(LokaToolboxBoxLayoutTests)
  set_target_properties(LokaToolboxBoxLayoutTests PROPERTIES CXX_STANDARD 98 CXX_STANDARD_REQUIRED YES)
  foreach(_box_path handler fallback)
    foreach(_box_case normal nested fixed empty small equal zero negative scroll-refusal scroll-limit natural-row)
      add_test(NAME testToolboxBox_${_box_path}_${_box_case}
        COMMAND LokaToolboxBoxLayoutTests ${_box_path} ${_box_case})
    endforeach()
  endforeach()

  foreach(_range_case scroll-refusal scroll-limit)
    foreach(_range_path handler fallback)
      foreach(_range_kind row row-advance row-offset grid)
        add_test(NAME testToolboxResult_${_range_kind}_${_range_path}_${_range_case}
          COMMAND LokaToolboxBoxLayoutTests ${_range_path} ${_range_kind} ${_range_case})
      endforeach()
    endforeach()
    add_test(NAME testToolboxResult_band_handler_${_range_case}
      COMMAND LokaToolboxBoxLayoutTests handler band ${_range_case})
  endforeach()

  foreach(_stack_axis column row)
    add_test(NAME testToolboxLayout_derived-${_stack_axis}
      COMMAND LokaToolboxBoxLayoutTests handler derived-${_stack_axis})
  endforeach()
  add_test(NAME testToolboxLayout_null-controller
    COMMAND LokaToolboxBoxLayoutTests null-controller abort)

  add_executable(LokaToolboxRectSurfaceTests
    tests/toolbox/host/ToolboxRectSurfaceHostTests.cpp
    apple/toolbox/src/context/ToolboxClipBoxContext.cpp
    apple/toolbox/src/ToolboxNodeDispatch.cpp
    apple/toolbox/src/ToolboxPlatformLayoutHandlers.cpp
    apple/toolbox/src/context/ToolboxProjectedNodeContext.cpp
    apple/toolbox/src/context/ToolboxRectSurfaceContext.cpp
    tests/TestingHooks.cpp)
  target_include_directories(LokaToolboxRectSurfaceTests PRIVATE tests/toolbox/host tests apple/toolbox/src)
  target_compile_definitions(LokaToolboxRectSurfaceTests PRIVATE LOKA_HOST_RECT_SURFACE)
  target_compile_options(LokaToolboxRectSurfaceTests PRIVATE
    -include ${PROJECT_SOURCE_DIR}/tests/toolbox/host/ToolboxBoxLayoutHost.hpp)
  target_link_libraries(LokaToolboxRectSurfaceTests PRIVATE LokaCommonTesting)
  loka_enable_warnings(LokaToolboxRectSurfaceTests)
  set_target_properties(LokaToolboxRectSurfaceTests PROPERTIES CXX_STANDARD 98 CXX_STANDARD_REQUIRED YES)
  foreach(_surface_case bottom right spacing limit normal scroll-bottom sprite-overflow sprite-normal sprite-outside sprite-negative dirty-current dirty-history region)
    add_test(NAME testToolboxRectSurface_${_surface_case} COMMAND LokaToolboxRectSurfaceTests ${_surface_case})
  endforeach()

  # Production QuickDraw projection against a deterministic host OS fixture.
  add_executable(LokaToolboxAttributedTextTests
    apple/toolbox/src/context/ToolboxRibbonGroupEdgeContext.cpp
    apple/toolbox/src/context/ToolboxCardBandContext.cpp
    apple/toolbox/src/context/ToolboxClipBoxContext.cpp
    tests/toolbox/host/ToolboxAttributedTextHostTests.cpp
    tests/toolbox/host/ToolboxHost.cpp
    apple/toolbox/src/ToolboxNaturalWidth.cpp
    tests/toolbox/host/ToolboxScrollViewHost.cpp
    apple/toolbox/src/ToolboxPlatformLayoutHandlers.cpp
    apple/toolbox/src/ToolboxString.cpp
    tests/TestingHooks.cpp
    apple/toolbox/src/context/ToolboxTextEditorContext.cpp
    apple/toolbox/src/context/ToolboxAttributedTextContext.cpp
    apple/toolbox/src/context/ToolboxAttributedTextTable.cpp
    apple/toolbox/src/platform/ToolboxPascalText.cpp
    apple/toolbox/src/platform/ToolboxMacRoman.cpp
    apple/toolbox/src/context/ToolboxLayoutUtil.cpp
    apple/toolbox/src/context/ToolboxProjectedNodeContext.cpp
    apple/toolbox/src/ToolboxBuiltInSupport.cpp)
  target_include_directories(LokaToolboxAttributedTextTests PRIVATE
    example/SmirkyCard/src
    tests/toolbox/host tests apple/toolbox/src)
  target_compile_options(LokaToolboxAttributedTextTests PRIVATE
    -include ${PROJECT_SOURCE_DIR}/tests/toolbox/host/ToolboxHost.hpp)
  target_link_libraries(LokaToolboxAttributedTextTests PRIVATE LokaCommonTesting)
  loka_enable_warnings(LokaToolboxAttributedTextTests)
  set_target_properties(LokaToolboxAttributedTextTests PROPERTIES CXX_STANDARD 98 CXX_STANDARD_REQUIRED YES)
  add_test(NAME testToolboxScrollViewX COMMAND LokaToolboxAttributedTextTests scroll-x)
  add_test(NAME testToolboxAttributedInk COMMAND LokaToolboxAttributedTextTests ink)
  add_test(NAME LokaToolboxAttributedTextTests COMMAND LokaToolboxAttributedTextTests)

  # Gap routing runs the real dispatch and ScrollView body against the OS host neighbors.
  get_target_property(_gap_host_sources LokaToolboxAttributedTextTests SOURCES)
  list(REMOVE_ITEM _gap_host_sources tests/toolbox/host/ToolboxAttributedTextHostTests.cpp)
  add_executable(LokaToolboxGapTests tests/toolbox/host/ToolboxGapHostTests.cpp
    apple/toolbox/src/debug/ToolboxSceneDebugStats.cpp
    apple/toolbox/src/ToolboxNodeDispatch.cpp ${_gap_host_sources})
  target_include_directories(LokaToolboxGapTests PRIVATE tests/toolbox/host tests apple/toolbox/src)
  target_compile_definitions(LokaToolboxGapTests PRIVATE LOKA_HOST_GAP_DISPATCH LOKA_HOST_CLIP_ALLOCATIONS)
  target_compile_options(LokaToolboxGapTests PRIVATE
    -include ${PROJECT_SOURCE_DIR}/tests/toolbox/host/ToolboxHost.hpp)
  target_link_libraries(LokaToolboxGapTests PRIVATE LokaCommonTesting)
  loka_enable_warnings(LokaToolboxGapTests)
  set_target_properties(LokaToolboxGapTests PROPERTIES CXX_STANDARD 98 CXX_STANDARD_REQUIRED YES)
  foreach(_gap_case column siblings resets row measurement origin edge spans overflow-default root clip-box clip-card clip-unflagged clip-ribbon clip-refusal clip-registry clip-lifecycle clip-placement clip-width clip-latest clip-nested clip-admission clip-projection clip-scroll clip-unit clip-card-scroll clip-grid clip-restart clip-partial clip-native clip-result clip-card-unconstrained)
    add_test(NAME testToolboxGap_${_gap_case} COMMAND LokaToolboxGapTests ${_gap_case})
  endforeach()

  add_executable(LokaToolboxTextEditorTests tests/toolbox/host/ToolboxTextEditorHostTests.cpp)
  target_compile_definitions(LokaToolboxTextEditorTests PRIVATE LOKA_HOST_EDITOR_SCROLL)
  get_target_property(_editor_host_sources LokaToolboxAttributedTextTests SOURCES)
  target_sources(LokaToolboxAttributedTextTests PRIVATE example/SmirkyCard/src/SmirkyMarkup.cpp)
  list(REMOVE_ITEM _editor_host_sources tests/toolbox/host/ToolboxAttributedTextHostTests.cpp)
  target_sources(LokaToolboxTextEditorTests PRIVATE ${_editor_host_sources})
  target_include_directories(LokaToolboxTextEditorTests PRIVATE tests/toolbox/host tests apple/toolbox/src)
  target_compile_options(LokaToolboxTextEditorTests PRIVATE -include ${PROJECT_SOURCE_DIR}/tests/toolbox/host/ToolboxHost.hpp)
  target_link_libraries(LokaToolboxTextEditorTests PRIVATE LokaCommonTesting)
  loka_enable_warnings(LokaToolboxTextEditorTests)
  set_target_properties(LokaToolboxTextEditorTests PROPERTIES CXX_STANDARD 98 CXX_STANDARD_REQUIRED YES)
  add_test(NAME LokaToolboxTextEditorTests COMMAND LokaToolboxTextEditorTests)
  foreach(_bar_case creation geometry replay-retained replay-composition metrics terminal refusal placement port-idle gesture inactive clipped-hit bulk retained unused)
    add_test(NAME testToolboxEditorBar_${_bar_case} COMMAND LokaToolboxTextEditorTests bar-${_bar_case})
  endforeach()
  foreach(_scroll_case codes view admission repair noop)
    add_test(NAME testToolboxScrollKey_${_scroll_case} COMMAND LokaToolboxTextEditorTests scroll-${_scroll_case})
  endforeach()
  add_test(NAME testToolboxTextEditorScrollBar COMMAND LokaToolboxTextEditorTests scroll-bar)
  add_test(NAME testToolboxTextEditorShrinkRepaint COMMAND LokaToolboxTextEditorTests shrink-repaint)
  add_test(NAME testToolboxTextEditorKeyClassification COMMAND LokaToolboxTextEditorTests key-classification)
  add_test(NAME testToolboxTextEditorIgnoredKeys COMMAND LokaToolboxTextEditorTests ignored-keys)
  add_test(NAME testToolboxTextEditorIgnorePhases COMMAND LokaToolboxTextEditorTests ignore-phases)
  add_test(NAME testToolboxTextEditorForwardDelete COMMAND LokaToolboxTextEditorTests forward-delete)
  add_test(NAME testToolboxTextEditorFrameInset COMMAND LokaToolboxTextEditorTests frame-inset)
  add_test(NAME testToolboxTextEditorPreContextRequest COMMAND LokaToolboxTextEditorTests pre-context-request)
  add_test(NAME testToolboxTextEditorPreContextRefused COMMAND LokaToolboxTextEditorTests pre-context-refused)

  add_executable(LokaToolboxPascalTextTests tests/toolbox/host/ToolboxPascalTextHostTests.cpp
    apple/toolbox/src/platform/ToolboxHfsName.cpp ${_editor_host_sources})
  target_include_directories(LokaToolboxPascalTextTests PRIVATE tests/toolbox/host tests apple/toolbox/src)
  target_compile_options(LokaToolboxPascalTextTests PRIVATE -include ${PROJECT_SOURCE_DIR}/tests/toolbox/host/ToolboxHost.hpp)
  target_link_libraries(LokaToolboxPascalTextTests PRIVATE LokaCommonTesting)
  loka_enable_warnings(LokaToolboxPascalTextTests)
  set_target_properties(LokaToolboxPascalTextTests PROPERTIES CXX_STANDARD 98 CXX_STANDARD_REQUIRED YES)
  foreach(_pascal_case codec malformed truncation script failure hfs counted decode host-stubs)
    add_test(NAME testToolboxPascal_${_pascal_case} COMMAND LokaToolboxPascalTextTests ${_pascal_case})
  endforeach()


  add_executable(LokaToolboxMenuTests
    tests/toolbox/host/ToolboxMenuAttachmentHostTests.cpp
    common/app/MenuComposition.cpp # Exercise C++98 return copies with elision disabled.
    tests/TestingHooks.cpp
    apple/toolbox/src/ToolboxMenuAttachment.cpp
    apple/toolbox/src/platform/ToolboxPascalText.cpp
    apple/toolbox/src/platform/ToolboxMacRoman.cpp)
  target_include_directories(LokaToolboxMenuTests PRIVATE tests/toolbox/host tests apple/toolbox/src)
  target_compile_definitions(LokaToolboxMenuTests PRIVATE LOKA_HOST_MENU_ATTACHMENT)
  target_compile_options(LokaToolboxMenuTests PRIVATE
    -include ${PROJECT_SOURCE_DIR}/tests/toolbox/host/ToolboxMenuHost.hpp -Wno-multichar -fno-elide-constructors)
  target_link_libraries(LokaToolboxMenuTests PRIVATE LokaCommonTesting)
  loka_enable_warnings(LokaToolboxMenuTests)
  set_target_properties(LokaToolboxMenuTests PROPERTIES CXX_STANDARD 98 CXX_STANDARD_REQUIRED YES)
  add_test(NAME LokaToolboxMenuTests COMMAND LokaToolboxMenuTests)

  # Refusal paths of the Toolbox picture -> Image allocations (#1064).
  add_executable(LokaToolboxNativeImageTests
    tests/toolbox/host/ToolboxNativeImageHostTests.cpp
    tests/TestingHooks.cpp
    apple/toolbox/src/ToolboxNativeImage.cpp
    apple/toolbox/src/PictParser.cpp
    apple/toolbox/src/ToolboxPlatformContext.cpp
    apple/toolbox/src/ToolboxBusy.cpp)
  target_include_directories(LokaToolboxNativeImageTests PRIVATE tests/toolbox/host/image tests/toolbox/host tests apple/toolbox/src)
  target_compile_options(LokaToolboxNativeImageTests PRIVATE
    -include ${PROJECT_SOURCE_DIR}/tests/toolbox/host/ToolboxImagePlatformHost.hpp)
  target_link_libraries(LokaToolboxNativeImageTests PRIVATE LokaCommonTesting)
  loka_enable_warnings(LokaToolboxNativeImageTests)
  set_target_properties(LokaToolboxNativeImageTests PROPERTIES CXX_STANDARD 98 CXX_STANDARD_REQUIRED YES)
  foreach(_native_image_case
      testBorrowedPictureKeptOnNativeImageRefusal
      testBorrowedPictureKeptOnImageRecordRefusal
      testBorrowedPictureKeptOnControlBlockRefusal
      testBorrowedPictureKeptAfterLastImageCopy
      testPictBytesRefusalsReleaseEverything
      testImageDecodeRetainsOutputOwnedBlob
      testPictSnapshotKeepsOnlyFilledRange
      testImageDecodeRejectsEmptyAndOutsideRanges)
    add_test(NAME ${_native_image_case} COMMAND LokaToolboxNativeImageTests ${_native_image_case})
  endforeach()
  add_test(NAME testToolboxAppShutdownReleasesMenuBeforeAttachmentDestruction
    COMMAND LokaToolboxMenuTests testToolboxAppShutdownReleasesMenuBeforeAttachmentDestruction)

  add_executable(LokaToolboxControlTests tests/toolbox/host/ToolboxControlHostTests.cpp ${_editor_host_sources})
  target_include_directories(LokaToolboxControlTests PRIVATE tests/toolbox/host tests apple/toolbox/src)
  target_compile_options(LokaToolboxControlTests PRIVATE -include ${PROJECT_SOURCE_DIR}/tests/toolbox/host/ToolboxHost.hpp)
  target_link_libraries(LokaToolboxControlTests PRIVATE LokaCommonTesting)
  loka_enable_warnings(LokaToolboxControlTests)
  set_target_properties(LokaToolboxControlTests PROPERTIES CXX_STANDARD 98 CXX_STANDARD_REQUIRED YES)
  foreach(_control_case app-page app-off-arrow app-cancel viewport-page viewport-off-arrow viewport-cancel
      native-hit setters lifetime clear ports te-movement)
    add_test(NAME testToolboxControl_${_control_case} COMMAND LokaToolboxControlTests ${_control_case})
  endforeach()

  add_executable(LokaToolboxFocusTests tests/toolbox/host/ToolboxFocusHostTests.cpp ${_editor_host_sources})
  target_include_directories(LokaToolboxFocusTests PRIVATE tests/toolbox/host tests apple/toolbox/src)
  target_compile_options(LokaToolboxFocusTests PRIVATE -include ${PROJECT_SOURCE_DIR}/tests/toolbox/host/ToolboxHost.hpp)
  target_link_libraries(LokaToolboxFocusTests PRIVATE LokaCommonTesting)
  loka_enable_warnings(LokaToolboxFocusTests)
  set_target_properties(LokaToolboxFocusTests PROPERTIES CXX_STANDARD 98 CXX_STANDARD_REQUIRED YES)
  add_test(NAME LokaToolboxFocusTests COMMAND LokaToolboxFocusTests)
  foreach(_tab_case geometry landing eligibility fallback decoder editor)
    add_test(NAME testToolboxTab_${_tab_case} COMMAND LokaToolboxFocusTests tab-${_tab_case})
  endforeach()
  add_executable(LokaToolboxInputDoorTests tests/toolbox/host/ToolboxInputDoorHostTests.cpp ${_editor_host_sources})
  target_include_directories(LokaToolboxInputDoorTests PRIVATE tests/toolbox/host tests apple/toolbox/src)
  target_compile_options(LokaToolboxInputDoorTests PRIVATE -include ${PROJECT_SOURCE_DIR}/tests/toolbox/host/ToolboxHost.hpp)
  target_link_libraries(LokaToolboxInputDoorTests PRIVATE LokaCommonTesting)
  loka_enable_warnings(LokaToolboxInputDoorTests)
  set_target_properties(LokaToolboxInputDoorTests PROPERTIES CXX_STANDARD 98 CXX_STANDARD_REQUIRED YES)
  add_test(NAME LokaToolboxInputDoorTests COMMAND LokaToolboxInputDoorTests)
  add_test(NAME testToolboxPopupSelectionWriteUsesOwnerTracker
    COMMAND LokaToolboxInputDoorTests popup-owner-tracker)
  add_test(NAME testToolboxScrollBarWriteUsesOwnerTracker
    COMMAND LokaToolboxInputDoorTests scroll-owner-tracker)

  add_executable(LokaToolboxCellPaintTests tests/toolbox/host/ToolboxCellPaintHostTests.cpp
    apple/toolbox/src/debug/ToolboxSceneDebugStats.cpp
    apple/toolbox/src/context/ToolboxCellContext.cpp ${_editor_host_sources})
  target_include_directories(LokaToolboxCellPaintTests PRIVATE tests/toolbox/host tests apple/toolbox/src)
  target_compile_definitions(LokaToolboxCellPaintTests PRIVATE LOKA_HOST_CELL_PAINT=1)
  target_compile_options(LokaToolboxCellPaintTests PRIVATE -include ${PROJECT_SOURCE_DIR}/tests/toolbox/host/ToolboxHost.hpp)
  target_link_libraries(LokaToolboxCellPaintTests PRIVATE LokaCommonTesting)
  loka_enable_warnings(LokaToolboxCellPaintTests)
  set_target_properties(LokaToolboxCellPaintTests PROPERTIES CXX_STANDARD 98 CXX_STANDARD_REQUIRED YES)
  add_test(NAME LokaToolboxCellPaintTests COMMAND LokaToolboxCellPaintTests)

  add_executable(LokaToolboxPaintFoldTests tests/toolbox/host/ToolboxPaintFoldHostTests.cpp
    apple/toolbox/src/debug/ToolboxSceneDebugStats.cpp
    apple/toolbox/src/context/ToolboxCellContext.cpp ${_editor_host_sources})
  target_include_directories(LokaToolboxPaintFoldTests PRIVATE tests/toolbox/host tests apple/toolbox/src)
  target_compile_definitions(LokaToolboxPaintFoldTests PRIVATE LOKA_HOST_CELL_PAINT=1)
  target_compile_options(LokaToolboxPaintFoldTests PRIVATE -include ${PROJECT_SOURCE_DIR}/tests/toolbox/host/ToolboxHost.hpp)
  target_link_libraries(LokaToolboxPaintFoldTests PRIVATE LokaCommonTesting)
  loka_enable_warnings(LokaToolboxPaintFoldTests)
  set_target_properties(LokaToolboxPaintFoldTests PROPERTIES CXX_STANDARD 98 CXX_STANDARD_REQUIRED YES)
  add_test(NAME LokaToolboxPaintFoldTests COMMAND LokaToolboxPaintFoldTests)

  add_executable(LokaToolboxTwoByteKeyTests tests/toolbox/host/ToolboxTwoByteKeyHostTests.cpp)
  target_include_directories(LokaToolboxTwoByteKeyTests PRIVATE tests apple/toolbox/src)
  loka_enable_warnings(LokaToolboxTwoByteKeyTests)
  set_target_properties(LokaToolboxTwoByteKeyTests PROPERTIES CXX_STANDARD 98 CXX_STANDARD_REQUIRED YES)
  add_test(NAME LokaToolboxTwoByteKeyTests COMMAND LokaToolboxTwoByteKeyTests)

  add_executable(LokaToolboxCpuClassifyTests tests/toolbox/host/ToolboxCpuClassifyHostTests.cpp)
  target_include_directories(LokaToolboxCpuClassifyTests PRIVATE common tests apple/toolbox/src)
  loka_enable_warnings(LokaToolboxCpuClassifyTests)
  set_target_properties(LokaToolboxCpuClassifyTests PROPERTIES CXX_STANDARD 98 CXX_STANDARD_REQUIRED YES)
  add_test(NAME LokaToolboxCpuClassifyTests COMMAND LokaToolboxCpuClassifyTests)

  add_executable(LokaToolboxBlurKernelTests tests/toolbox/host/ToolboxBlurKernelHostTests.cpp apple/toolbox/src/ToolboxBlurKernel.cpp)
  target_include_directories(LokaToolboxBlurKernelTests PRIVATE common tests apple/toolbox/src)
  loka_enable_warnings(LokaToolboxBlurKernelTests)
  set_target_properties(LokaToolboxBlurKernelTests PROPERTIES CXX_STANDARD 98 CXX_STANDARD_REQUIRED YES)
  add_test(NAME LokaToolboxBlurKernelTests COMMAND LokaToolboxBlurKernelTests)

  add_executable(LokaToolboxControlWidthTests tests/toolbox/host/ToolboxControlWidthHostTests.cpp
    tests/toolbox/host/ToolboxRowSpacingHostTests.cpp
    apple/toolbox/src/context/ToolboxButtonContext.cpp
    apple/toolbox/src/context/ToolboxEditTextContext.cpp
    apple/toolbox/src/context/ToolboxPopupMenuContext.cpp ${_editor_host_sources})
  target_include_directories(LokaToolboxControlWidthTests PRIVATE tests/toolbox/host tests apple/toolbox/src)
  target_compile_definitions(LokaToolboxControlWidthTests PRIVATE LOKA_HOST_CONTROL_WIDTH=1)
  target_compile_options(LokaToolboxControlWidthTests PRIVATE -include ${PROJECT_SOURCE_DIR}/tests/toolbox/host/ToolboxHost.hpp)
  target_link_libraries(LokaToolboxControlWidthTests PRIVATE LokaCommonTesting)
  loka_enable_warnings(LokaToolboxControlWidthTests)
  set_target_properties(LokaToolboxControlWidthTests PROPERTIES CXX_STANDARD 98 CXX_STANDARD_REQUIRED YES)
  foreach(_control_width_case natural-image spacing-text-natural spacing-group spacing-caption-wins spacing-ribbon spacing-unaligned spacing-aligned spacing-column encoding-button encoding-popup encoding-text encoding-plain encoding-wrap encoding-ellipsis column-button column-edit column-popup natural row-title natural-title equal-title clipped-title)
    add_test(NAME testToolboxControlWidth_${_control_width_case} COMMAND LokaToolboxControlWidthTests ${_control_width_case})
  endforeach()

  add_executable(LokaToolboxControlOrderTests tests/toolbox/host/ToolboxControlOrderHostTests.cpp
    apple/toolbox/src/context/ToolboxScrollBarContext.cpp
    apple/toolbox/src/context/ToolboxCellContext.cpp
    apple/toolbox/src/context/ToolboxButtonContext.cpp
    apple/toolbox/src/context/ToolboxEditTextContext.cpp
    apple/toolbox/src/context/ToolboxPopupMenuContext.cpp ${_editor_host_sources})
  target_include_directories(LokaToolboxControlOrderTests PRIVATE tests/toolbox/host tests apple/toolbox/src)
  target_compile_definitions(LokaToolboxControlOrderTests PRIVATE LOKA_HOST_CONTROL_WIDTH=1 LOKA_HOST_CELL_PAINT=1 LOKA_HOST_CONTROL_ORDER=1)
  target_compile_options(LokaToolboxControlOrderTests PRIVATE -include ${PROJECT_SOURCE_DIR}/tests/toolbox/host/ToolboxHost.hpp)
  target_link_libraries(LokaToolboxControlOrderTests PRIVATE LokaCommonTesting)
  loka_enable_warnings(LokaToolboxControlOrderTests)
  set_target_properties(LokaToolboxControlOrderTests PROPERTIES CXX_STANDARD 98 CXX_STANDARD_REQUIRED YES)
  foreach(_order_case ButtonHiddenMove ButtonCreatedPaintsOnce ButtonMoveWithoutErasedPass PressDoor ParkedNativeButton RetainedPropsReconcileDoesNotPaint ButtonDrawnAtCompositionPosition ButtonRefusedLabelDrawsInstalledControl ViewportBarsDrawnByRenderScrollView CompositionReplayDrawsControlsInWalk ScrollBarDrawnAtRender)
    add_test(NAME testToolbox${_order_case} COMMAND LokaToolboxControlOrderTests ${_order_case})
  endforeach()

  add_executable(LokaToolboxZStackUnitTests tests/toolbox/host/ToolboxZStackUnitHostTests.cpp
    apple/toolbox/src/ToolboxNodeDispatch.cpp
    apple/toolbox/src/context/ToolboxScrollBarContext.cpp
    apple/toolbox/src/context/ToolboxCellContext.cpp
    apple/toolbox/src/context/ToolboxButtonContext.cpp
    apple/toolbox/src/context/ToolboxEditTextContext.cpp
    apple/toolbox/src/context/ToolboxPopupMenuContext.cpp ${_editor_host_sources})
  target_include_directories(LokaToolboxZStackUnitTests PRIVATE tests/toolbox/host tests apple/toolbox/src)
  target_compile_definitions(LokaToolboxZStackUnitTests PRIVATE LOKA_HOST_CONTROL_WIDTH=1 LOKA_HOST_CELL_PAINT=1 LOKA_HOST_CONTROL_ORDER=1 LOKA_HOST_GAP_DISPATCH=1 LOKA_HOST_EDITOR_SCROLL=1 LOKA_HOST_ZSTACK_UNITS=1)
  target_compile_options(LokaToolboxZStackUnitTests PRIVATE -include ${PROJECT_SOURCE_DIR}/tests/toolbox/host/ToolboxHost.hpp)
  target_link_libraries(LokaToolboxZStackUnitTests PRIVATE LokaCommonTesting)
  loka_enable_warnings(LokaToolboxZStackUnitTests)
  set_target_properties(LokaToolboxZStackUnitTests PROPERTIES CXX_STANDARD 98 CXX_STANDARD_REQUIRED YES)
  foreach(_order_case ClipFrameProjection BandGeometry BandLifetime BandDelivery BandSelection StaticNoRequests StaticRefusedReplay StaticCopyFailure FlushFirstCapture StaticOpportunities ErasedAdmissionMatrix StaticFirstUnit StaticAdmission StaticExternalDraw StaticFailure StaticIneligible StaticMissingRow StaticChangedRect StaticChangedGround StaticAbortedLayout FrostErasedPassAdmission HeightOnlyBoxUnit WidthOnlyBoxDispatch TextDirtyClipStaysBelowUnit IdleRepairOnlyToggledOverlaps LaterChildGetsUnitRow UnitErasesGroundBeforeChild UnitClipsChild RefusedChildHasNoRowAndNoRender NestedZStackInUnitLaysOutNothing ScrollViewRefusedInsideUnit UnitInsideViewportRowIsProjected DirtyTouchingUnitTakesCompositionReplay LedgerRebuiltEachPass UnitFrameInheritsParentClip RetiredGroundHideSuppressed)
    add_test(NAME testToolbox${_order_case} COMMAND LokaToolboxZStackUnitTests ${_order_case})
  endforeach()

  add_executable(LokaToolboxPartialPaintTests tests/toolbox/host/ToolboxPartialPaintHostTests.cpp
    apple/toolbox/src/context/ToolboxCellContext.cpp
    apple/toolbox/src/context/ToolboxButtonContext.cpp
    apple/toolbox/src/context/ToolboxEditTextContext.cpp
    apple/toolbox/src/context/ToolboxPopupMenuContext.cpp ${_editor_host_sources})
  target_include_directories(LokaToolboxPartialPaintTests PRIVATE tests/toolbox/host tests apple/toolbox/src)
  target_compile_definitions(LokaToolboxPartialPaintTests PRIVATE LOKA_HOST_CONTROL_WIDTH=1 LOKA_HOST_CELL_PAINT=1)
  target_compile_options(LokaToolboxPartialPaintTests PRIVATE -include ${PROJECT_SOURCE_DIR}/tests/toolbox/host/ToolboxHost.hpp)
  target_link_libraries(LokaToolboxPartialPaintTests PRIVATE LokaCommonTesting)
  loka_enable_warnings(LokaToolboxPartialPaintTests)
  set_target_properties(LokaToolboxPartialPaintTests PROPERTIES CXX_STANDARD 98 CXX_STANDARD_REQUIRED YES)
  add_test(NAME testToolboxPlainInk COMMAND LokaToolboxPartialPaintTests text-ink)
  foreach(_partial_paint_case cell-encoding cell text text-repaint button button-repaint popup popup-repaint attributed edit edit-refused)
    add_test(NAME testToolboxPartialPaint_${_partial_paint_case} COMMAND LokaToolboxPartialPaintTests ${_partial_paint_case})
  endforeach()

  add_test(NAME testToolboxParkedEditorPress COMMAND LokaToolboxTextEditorTests parked-press)
  add_test(NAME testToolboxParkedEditorKeys COMMAND LokaToolboxTextEditorTests parked-keys)
  add_test(NAME testToolboxEditorBarDrawnAtRender COMMAND LokaToolboxTextEditorTests render-bar)

  foreach(_editor_request_case input-door request report project input clear epilogue refuse clamp
      take-edit report-edit detach rebind nested retry endless five-reposts
      trace reply-clamp reply-stale empty-settle take-refused-edit reply-edit scroll-cleanup retire-render retire-key retire-click retire-paste refused-report)
    add_test(NAME testToolboxEditorRequest_${_editor_request_case}
      COMMAND LokaToolboxTextEditorTests ${_editor_request_case})
  endforeach()

  # Production GDI table with deterministic host calls; native HWND pins remain in LokaTestsWin32.
  add_executable(LokaWin32AttributedTextHostTests
    tests/win32/host/Win32AttributedTextHostTests.cpp
    tests/TestingHooks.cpp
    win32/src/context/Win32AttributedTextTable.cpp)
  target_include_directories(LokaWin32AttributedTextHostTests PRIVATE tests/win32/host tests win32/src)
  target_compile_options(LokaWin32AttributedTextHostTests PRIVATE
    -include ${PROJECT_SOURCE_DIR}/tests/win32/host/Win32TextHost.hpp)
  target_link_libraries(LokaWin32AttributedTextHostTests PRIVATE LokaCommonTesting)
  loka_enable_warnings(LokaWin32AttributedTextHostTests)
  set_target_properties(LokaWin32AttributedTextHostTests PROPERTIES CXX_STANDARD 98 CXX_STANDARD_REQUIRED YES)
  add_test(NAME LokaWin32AttributedTextHostTests COMMAND LokaWin32AttributedTextHostTests)

  # Isolated S1 pool simulation: deny Source and count global new during reclaim.
  add_executable(LokaReclaimGaugePins tests/ReclaimGaugeMain.cpp tests/ReclaimScratchTests.cpp)
  loka_enable_warnings(LokaReclaimGaugePins)
  target_include_directories(LokaReclaimGaugePins PRIVATE tests)
  target_compile_definitions(LokaReclaimGaugePins PRIVATE TEST_BUILD LOKA_RECLAIM_GAUGE_PIN)
  set_target_properties(LokaReclaimGaugePins PROPERTIES CXX_STANDARD 98 CXX_STANDARD_REQUIRED YES)
  target_link_libraries(LokaReclaimGaugePins PRIVATE LokaCommonTesting)
  add_test(NAME LokaReclaimGaugePins COMMAND LokaReclaimGaugePins)

  # Host-command regression fixture. This is the production lrpc entry point,
  # built only into the native test graph so command tests exercise the real
  # create/write/cleanup/rename path rather than a duplicated file helper.
  add_executable(LokaLrpcTestTool
    ${PROJECT_SOURCE_DIR}/tools/lrpc/main.cpp
    ${PROJECT_SOURCE_DIR}/tools/lrpc/HostFile.cpp
    ${PROJECT_SOURCE_DIR}/tools/lrpc/LrpkStager.cpp
    ${PROJECT_SOURCE_DIR}/tools/lrpc/PackManifest.cpp
    ${PROJECT_SOURCE_DIR}/tools/lrpc/ResourceHeader.cpp
    ${PROJECT_SOURCE_DIR}/tools/lrpc/LrpkWriter.cpp
    ${PROJECT_SOURCE_DIR}/tools/lrpc/Utf8Path.cpp
  )
  loka_enable_warnings(LokaLrpcTestTool)
  target_include_directories(LokaLrpcTestTool PRIVATE
    ${PROJECT_SOURCE_DIR}/common
    ${PROJECT_SOURCE_DIR}/tools
  )
  set_target_properties(LokaLrpcTestTool PROPERTIES
    CXX_STANDARD 98
    CXX_STANDARD_REQUIRED YES
  )
  target_compile_definitions(LokaLrpcTestTool PRIVATE
    LOKA_LRPC_TESTING=1
  )

  function(loka_add_registered_test test_line target)
    string(REGEX REPLACE
      "^[ \t]*LOKA_TEST\\(([A-Za-z_][A-Za-z0-9_:]*)\\)[ \t]*$"
      "\\1" _loka_test_name "${test_line}")
    string(REGEX REPLACE "[^A-Za-z0-9_.-]" "_"
      _loka_test_work_name "${_loka_test_name}")
    set(_loka_test_work_dir
      "${CMAKE_CURRENT_BINARY_DIR}/test-work/${_loka_test_work_name}")
    file(MAKE_DIRECTORY "${_loka_test_work_dir}")
    add_test(NAME "${_loka_test_name}"
      COMMAND "$<TARGET_FILE:${target}>" "${_loka_test_name}")
    set_tests_properties("${_loka_test_name}" PROPERTIES
      WORKING_DIRECTORY "${_loka_test_work_dir}")
    if(_loka_test_name MATCHES "^testExpectedRed")
      set_tests_properties("${_loka_test_name}" PROPERTIES
        LABELS "expected-red" SKIP_RETURN_CODE 77)
    endif()
  endfunction()

  # Runs a whole binary with no arguments, in its own directory under
  # test-work/ like the per-test entries of loka_add_registered_test above.
  function(loka_add_whole_binary_test test_name target)
    set(_loka_test_work_dir
      "${CMAKE_CURRENT_BINARY_DIR}/test-work/${test_name}")
    file(MAKE_DIRECTORY "${_loka_test_work_dir}")
    add_test(NAME "${test_name}"
      COMMAND "$<TARGET_FILE:${target}>")
    set_tests_properties("${test_name}" PROPERTIES
      WORKING_DIRECTORY "${_loka_test_work_dir}")
  endfunction()

  if(NOT WIN32)
    add_test(NAME scriptMameDebugLaunch
      COMMAND bash ${PROJECT_SOURCE_DIR}/tests/scripts/MameDebugLaunchTest.sh)
    add_test(NAME scriptMameRunLaunch
      COMMAND bash ${PROJECT_SOURCE_DIR}/tests/scripts/MameRunLaunchTest.sh)
    add_test(NAME scriptToolboxStandaloneRunner
      COMMAND bash ${PROJECT_SOURCE_DIR}/tests/scripts/ToolboxStandaloneRunnerTest.sh)
    add_test(NAME scriptRetro68Environment
      COMMAND bash ${PROJECT_SOURCE_DIR}/tests/scripts/Retro68EnvironmentTest.sh)
    add_test(NAME scriptRetro68ToolchainFlavor
      COMMAND bash ${PROJECT_SOURCE_DIR}/tests/scripts/Retro68ToolchainFlavorTest.sh)
    add_test(NAME scriptCiPaths
      COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/scripts/CiPathsTest.py)
    add_test(NAME scriptRetro68SizeReport
      COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/scripts/Retro68SizeReportTest.py)
    add_test(NAME scriptRetro68SizeBankRefresh
      COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/scripts/Retro68SizeBankRefreshTest.py)
    add_test(NAME scriptPngCropSelftest
      COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/scenarios/pngtool.py selftest)
    add_test(NAME scriptScenarioTools
      COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/scripts/ScenarioToolsTest.py)
    add_test(NAME scriptNativeBuildPicker
      COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/scripts/NativeBuildPickerTest.py)
    add_test(NAME scriptReleaseAssembler
      COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/scripts/ReleaseAssemblerTest.py)
    add_test(NAME scriptWin32RemoteStage
      COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/scripts/Win32RemoteStageTest.py)
    add_test(NAME scriptWin32RemoteStagePowerShell
      COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/scripts/Win32RemoteStagePowerShellTest.py)
    add_test(NAME scriptPresentationStage
      COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/scripts/PresentationStageTest.py)
    add_test(NAME scriptMacOSStandaloneAuditVerifier
      COMMAND python3
        ${PROJECT_SOURCE_DIR}/tests/scripts/MacOSStandaloneAuditVerifierTest.py)
    add_test(NAME scriptToolboxStandaloneStage
      COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/scripts/ToolboxStandaloneStageTest.py)
    add_test(NAME scriptMacOSRigProtocol
      COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/scripts/MacOSRigProtocolTest.py)
    add_test(NAME scriptLokaRig
      COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/scripts/LokaRigTest.py)
    add_test(NAME scriptToolboxPresentationRail
      COMMAND bash ${PROJECT_SOURCE_DIR}/tests/scripts/ToolboxPresentationRailTest.sh)
    add_test(NAME scriptToolboxScenarioRunner
      COMMAND bash ${PROJECT_SOURCE_DIR}/tests/scripts/ToolboxScenarioRunnerTest.sh)
    add_test(NAME scriptToolboxStandaloneAuditVerifier
      COMMAND bash ${PROJECT_SOURCE_DIR}/tests/scripts/ToolboxStandaloneAuditVerifierTest.sh)
    add_test(NAME scriptMacOSScenarioRunner
      COMMAND bash ${PROJECT_SOURCE_DIR}/tests/scripts/MacOSScenarioRunnerTest.sh)
    add_test(NAME scriptBuild104Guard
      COMMAND bash ${PROJECT_SOURCE_DIR}/tests/scripts/Build104GuardTest.sh)
    add_test(NAME scriptMacOSBuildTargets
      COMMAND bash ${PROJECT_SOURCE_DIR}/tests/scripts/MacOSBuildTargetsTest.sh)
    add_test(NAME scriptMacOSStandaloneUb1Release
      COMMAND bash
        ${PROJECT_SOURCE_DIR}/tests/scripts/MacOSStandaloneUb1ReleaseTest.sh)
    add_test(NAME scriptMacOSStandaloneUb2Release
      COMMAND bash
        ${PROJECT_SOURCE_DIR}/tests/scripts/MacOSStandaloneUb2ReleaseTest.sh)
    add_test(NAME scriptPmonSpriteGenerator
      COMMAND python3
        ${PROJECT_SOURCE_DIR}/tests/scripts/PmonSpriteGeneratorTest.py)
    add_test(NAME scriptLrpcStageWriteFailure
      COMMAND python3
        ${PROJECT_SOURCE_DIR}/tests/scripts/LrpcStageWriteFailureTest.py
        $<TARGET_FILE:LokaLrpcTestTool>
        ${PROJECT_SOURCE_DIR}/example/ScrapbookUI/ASSETS.LRP)
    add_test(NAME scriptLrpcResourceHeader
      COMMAND python3
        ${PROJECT_SOURCE_DIR}/tests/scripts/LrpcResourceHeaderTest.py
        $<TARGET_FILE:LokaLrpcTestTool>)

    if(LOKA_MAME_SCENARIOS)
      file(STRINGS
        ${PROJECT_SOURCE_DIR}/tests/scenarios/scenarios.txt
        _loka_mame_scenarios
        REGEX "^[a-z0-9][a-z0-9-]* [a-z0-9][a-z0-9-]*$")
      foreach(_loka_mame_entry IN LISTS _loka_mame_scenarios)
        string(REGEX REPLACE "^([^ ]+) ([^ ]+)$" "\\1"
          _loka_mame_example "${_loka_mame_entry}")
        string(REGEX REPLACE "^([^ ]+) ([^ ]+)$" "\\2"
          _loka_mame_scenario "${_loka_mame_entry}")
        add_test(NAME "mameScenario-${_loka_mame_example}-${_loka_mame_scenario}"
          COMMAND bash ${PROJECT_SOURCE_DIR}/tests/toolbox/run-scenario.sh
            "${_loka_mame_example}" "${_loka_mame_scenario}")
        set_tests_properties("mameScenario-${_loka_mame_example}-${_loka_mame_scenario}"
          PROPERTIES LABELS "mame")
      endforeach()

      # The watchpoint legs share the fixed build/mame-debug staging area of
      # scripts/mame-debug.sh, so they must not run concurrently.
      foreach(_loka_wpset_mode clean control)
        add_test(NAME "mameScenarioWpset-${_loka_wpset_mode}"
          COMMAND bash ${PROJECT_SOURCE_DIR}/tests/toolbox/run-wpset.sh
            "${_loka_wpset_mode}")
        set_tests_properties("mameScenarioWpset-${_loka_wpset_mode}"
          PROPERTIES LABELS "mame" RUN_SERIAL TRUE)
      endforeach()
    endif()
  endif()

  # The registries are configure inputs: editing a LOKA_TEST line must
  # regenerate CTestTestfile.cmake, not just rebuild the runner binaries.
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/tests/ContractTestRegistry.inc
    ${PROJECT_SOURCE_DIR}/tests/ScenarioTestRegistry.inc
    ${PROJECT_SOURCE_DIR}/tests/scenarios/scenarios.txt
  )

  file(STRINGS ${PROJECT_SOURCE_DIR}/tests/ContractTestRegistry.inc
    _loka_contract_test_lines
    REGEX "^[ \t]*(LOKA_TEST\\([A-Za-z_][A-Za-z0-9_:]*\\)|LOKA_SCENARIO_TESTS_HERE\\(\\))[ \t]*$")
  file(STRINGS ${PROJECT_SOURCE_DIR}/tests/ScenarioTestRegistry.inc
    _loka_scenario_test_lines
    REGEX "^[ \t]*LOKA_TEST\\([A-Za-z_][A-Za-z0-9_:]*\\)[ \t]*$")

  foreach(_loka_contract_test_line IN LISTS _loka_contract_test_lines)
    if(_loka_contract_test_line MATCHES "LOKA_SCENARIO_TESTS_HERE")
      foreach(_loka_scenario_test_line IN LISTS _loka_scenario_test_lines)
        loka_add_registered_test("${_loka_scenario_test_line}" LokaScenarioTests)
      endforeach()
    else()
      loka_add_registered_test("${_loka_contract_test_line}" LokaContractTests)
    endif()
  endforeach()

  # The pin binary runs whole (single scenario), matching the per-test
  # working-directory convention of the registered suites.
  loka_add_whole_binary_test(testZeroAllocationPin LokaAllocPinTests)

  # One-process runs (#922). The per-test entries above give every test a
  # fresh process. With no arguments a runner executes every test it
  # registers in one process, which is how ctest runs the macOS and Win32
  # test executables. State that outlives its test, or a check that holds
  # only in a fresh process, can fail here while every per-test entry passes.
  loka_add_whole_binary_test(oneProcessContractRunner LokaContractTests)
  loka_add_whole_binary_test(oneProcessScenarioRunner LokaScenarioTests)
endif()

if(LOKA_BUILD_SMIRKYCARD)
  enable_language(C)
  add_subdirectory(example/SmirkyCard)
  # The standalone release presets turn SmirkyCard on and stage it beside the loops.
  if(WIN32 AND TARGET LokaStandaloneLoopWin32All)
    add_dependencies(LokaStandaloneLoopWin32All LokaSmirkyCardWin32)
  endif()
endif()

# Pin the target-local warning contract after every platform/example target
# has been declared. New Loka targets must opt in explicitly.
loka_verify_warning_floor()
