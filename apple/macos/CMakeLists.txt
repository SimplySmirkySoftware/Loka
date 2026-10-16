cmake_minimum_required(VERSION 3.15)

set(CMAKE_CXX_STANDARD 98)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 98)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

option(TEST_BUILD "Include unit test sources in app target" OFF)
option(LOKA_MAC_OS_10_4 "Enable macOS 10.4u SDK mode" OFF)
if(NOT COMMAND loka_enable_standalone_performance)
  include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/LokaStandalonePerformance.cmake")
endif()
set(LOKA_MAC_OS_10_4_SYSROOT "/Developer/SDKs/MacOSX10.4u.sdk" CACHE PATH
  "Path to macOS 10.4u SDK used when LOKA_MAC_OS_10_4=ON")

if(LOKA_MAC_OS_10_4)
  if(NOT EXISTS "${LOKA_MAC_OS_10_4_SYSROOT}")
    message(FATAL_ERROR "LOKA_MAC_OS_10_4_SYSROOT not found: ${LOKA_MAC_OS_10_4_SYSROOT}")
  endif()
  set(CMAKE_OSX_SYSROOT "${LOKA_MAC_OS_10_4_SYSROOT}" CACHE PATH "macOS SDK path" FORCE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.4" CACHE STRING "Minimum macOS deployment target" FORCE)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options("-stdlib=libstdc++")
    add_link_options("-stdlib=libstdc++")
  endif()
endif()

add_library(LokaAppleMacOSAppKit INTERFACE)

target_link_libraries(LokaAppleMacOSAppKit INTERFACE
  LokaAppleCommonCocoa
  "-framework ApplicationServices"
  "-framework AppKit"
  "-framework Foundation"
  "-framework CoreFoundation"
)

target_include_directories(LokaAppleMacOSAppKit INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}
)

file(GLOB_RECURSE COMMON_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/common/*.cpp)
file(GLOB_RECURSE COMMON_HEADERS CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/common/*.hpp)
file(GLOB_RECURSE LOKA_MACOS_CORE_HEADERS CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_LIST_DIR}/src/*.hpp
  ${PROJECT_SOURCE_DIR}/apple/common/cocoa/src/*.hpp
)

set(LOKA_MACOS_CORE_SOURCES
  src/MacBootstrap.mm
  src/MacApp.mm
  src/MacMenuAttachment.mm
  src/MacBuiltInSupport.mm
  src/MacGround.mm
  src/MacDisplayAppearance.mm
  src/MacBitmapCapture.mm
  src/MacPlatformContext.mm
  src/MacProfiler.cpp
  src/MacScenePlatformController.mm
  src/MacWindow.mm
  src/platform/MacProjection.mm
  src/platform/MacAppLocation.mm
  src/context/MacButtonContext.mm
  src/context/MacRetirableContext.mm
  src/context/MacCellContext.mm
  src/MacCardPainter.mm
  src/context/MacCardBandContext.mm
  src/context/MacRibbonGroupEdgeContext.mm
  src/context/MacEditTextContext.mm
  src/context/MacTextEditorContext.mm
  src/context/MacOpenFileDialogContext.mm
  src/context/MacConfirmDialogContext.mm
  src/context/MacDeferredDialogPresenter.mm
  src/context/MacTextContext.mm
  src/context/MacAttributedTextContext.mm
  src/context/MacPopupMenuContext.mm
  src/context/MacImageViewContext.mm
  src/context/MacRectSurfaceContext.mm
  src/context/MacScrollViewContext.mm
  ${PROJECT_SOURCE_DIR}/apple/common/cocoa/src/AppleString.mm
)

add_library(LokaMacOSCore STATIC
  ${LOKA_MACOS_CORE_SOURCES}
  ${COMMON_SOURCES}
  ${COMMON_HEADERS}
  ${LOKA_MACOS_CORE_HEADERS}
)
loka_enable_warnings(LokaMacOSCore)

target_include_directories(LokaMacOSCore PUBLIC
  ${PROJECT_SOURCE_DIR}/common
)

target_include_directories(LokaMacOSCore PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/src
  ${CMAKE_CURRENT_LIST_DIR}/src/context
)

target_link_libraries(LokaMacOSCore PUBLIC LokaAppleMacOSAppKit)

# Loka core is ObjC1/non-ARC by design; do not force ARC flags here.

file(GLOB_RECURSE LOKA_MACOS_TEST_HEADERS CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/tests/*.hpp)

if(NOT LOKA_MAC_OS_10_4)
  # The test binary compiles the common sources itself so TEST_BUILD applies
  # to them uniformly (mirrors the Linux LokaFlowDslTests target). Linking the
  # production LokaMacOSCore instead would mix TEST_BUILD and non-TEST_BUILD
  # compilations of the same classes (ODR/layout divergence).
  file(GLOB_RECURSE LOKA_MACOS_TEST_COMMON_SOURCES CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/common/*.cpp
  )
  # Debug Build All must include this executable so VSCode CMake Tools can
  # build it before Test Explorer invokes ctest. Keep it excluded in Release
  # and multi-config projects: these assert-based tests require Debug, and the
  # macos-tests preset still builds the target explicitly when needed.
  set(LOKA_MACOS_TEST_EXCLUDE_OPTION EXCLUDE_FROM_ALL)
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(LOKA_MACOS_TEST_EXCLUDE_OPTION)
  endif()
  add_executable(LokaTestsMacOS ${LOKA_MACOS_TEST_EXCLUDE_OPTION}
    ${LOKA_SHARED_TEST_SOURCES}
    ${LOKA_MACOS_TEST_COMMON_SOURCES}
    ${PROJECT_SOURCE_DIR}/tests/MacDisplayAppearanceTests.mm
    ${PROJECT_SOURCE_DIR}/tests/MacAppLocationTests.mm
    ${PROJECT_SOURCE_DIR}/tests/MacImageBlobTests.mm
    ${PROJECT_SOURCE_DIR}/tests/MacMenuProjectionTests.mm
    ${PROJECT_SOURCE_DIR}/tests/MacNativeResourceRetirementTests.mm
    ${PROJECT_SOURCE_DIR}/tests/MacNodeHandlerEnsureTests.mm
    ${PROJECT_SOURCE_DIR}/tests/MacNaturalWidthTests.mm
    ${PROJECT_SOURCE_DIR}/tests/MacInputDoorTests.mm
    ${PROJECT_SOURCE_DIR}/tests/MacTextEditorTests.mm
    ${PROJECT_SOURCE_DIR}/tests/MacScrollViewTests.mm
    ${PROJECT_SOURCE_DIR}/tests/MacFocusTests.mm
    ${CMAKE_CURRENT_LIST_DIR}/src/MacBitmapCapture.mm
    ${CMAKE_CURRENT_LIST_DIR}/src/MacPlatformContext.mm
    ${CMAKE_CURRENT_LIST_DIR}/src/MacApp.mm
    ${PROJECT_SOURCE_DIR}/tests/MacWindowAdmissionTests.mm
    ${PROJECT_SOURCE_DIR}/tests/MacWindowFrameClampTests.mm
    ${PROJECT_SOURCE_DIR}/tests/MacWindowResizeTests.mm
    ${PROJECT_SOURCE_DIR}/tests/MacOpenFileDialogTests.mm
    ${PROJECT_SOURCE_DIR}/tests/MacConfirmDialogTests.mm
    ${CMAKE_CURRENT_LIST_DIR}/src/MacWindow.mm
    ${PROJECT_SOURCE_DIR}/apple/common/cocoa/src/AppleString.mm
    ${PROJECT_SOURCE_DIR}/tests/MacCardPainterTests.mm
    ${PROJECT_SOURCE_DIR}/tests/MacGroundTests.mm
    ${PROJECT_SOURCE_DIR}/tests/MacTextColorTests.mm
    ${CMAKE_CURRENT_LIST_DIR}/src/MacGround.mm
    ${CMAKE_CURRENT_LIST_DIR}/src/MacDisplayAppearance.mm
    ${CMAKE_CURRENT_LIST_DIR}/src/platform/MacAppLocation.mm
    ${CMAKE_CURRENT_LIST_DIR}/src/platform/MacProjection.mm
    ${CMAKE_CURRENT_LIST_DIR}/src/MacMenuAttachment.mm
    ${CMAKE_CURRENT_LIST_DIR}/src/MacBuiltInSupport.mm
    ${CMAKE_CURRENT_LIST_DIR}/src/MacScenePlatformController.mm
    ${CMAKE_CURRENT_LIST_DIR}/src/context/MacButtonContext.mm
    ${CMAKE_CURRENT_LIST_DIR}/src/context/MacRetirableContext.mm
    ${CMAKE_CURRENT_LIST_DIR}/src/context/MacCellContext.mm
    ${CMAKE_CURRENT_LIST_DIR}/src/MacCardPainter.mm
    ${CMAKE_CURRENT_LIST_DIR}/src/context/MacCardBandContext.mm
    ${CMAKE_CURRENT_LIST_DIR}/src/context/MacRibbonGroupEdgeContext.mm
    ${CMAKE_CURRENT_LIST_DIR}/src/context/MacEditTextContext.mm
    ${CMAKE_CURRENT_LIST_DIR}/src/context/MacTextEditorContext.mm
    ${CMAKE_CURRENT_LIST_DIR}/src/context/MacOpenFileDialogContext.mm
    ${CMAKE_CURRENT_LIST_DIR}/src/context/MacConfirmDialogContext.mm
    ${CMAKE_CURRENT_LIST_DIR}/src/context/MacDeferredDialogPresenter.mm
    ${CMAKE_CURRENT_LIST_DIR}/src/context/MacTextContext.mm
    ${CMAKE_CURRENT_LIST_DIR}/src/context/MacAttributedTextContext.mm
    ${CMAKE_CURRENT_LIST_DIR}/src/context/MacPopupMenuContext.mm
    ${CMAKE_CURRENT_LIST_DIR}/src/context/MacImageViewContext.mm
    ${CMAKE_CURRENT_LIST_DIR}/src/context/MacRectSurfaceContext.mm
    ${CMAKE_CURRENT_LIST_DIR}/src/context/MacScrollViewContext.mm
    ${LOKA_MACOS_TEST_HEADERS}
  )
  loka_enable_warnings(LokaTestsMacOS)

  target_include_directories(LokaTestsMacOS PRIVATE
    ${LOKA_SCRAPBOOK_R_HEADER_DIR}
    ${PROJECT_SOURCE_DIR}/common
    ${PROJECT_SOURCE_DIR}/tests
    ${PROJECT_SOURCE_DIR}/tools
    ${PROJECT_SOURCE_DIR}/example/HelloWorld/src
    ${PROJECT_SOURCE_DIR}/example/Tutorial/src
    ${PROJECT_SOURCE_DIR}/example/MineSweeper/src
    ${PROJECT_SOURCE_DIR}/example/FloppyBird/src
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/src/context
  )

  target_compile_definitions(LokaTestsMacOS PRIVATE TEST_BUILD)
  target_link_libraries(LokaTestsMacOS PRIVATE
    "-framework ApplicationServices"
    "-framework AppKit"
    "-framework Foundation"
    "-framework CoreFoundation"
  )

  enable_testing()
  add_test(NAME LokaTestsMacOS COMMAND LokaTestsMacOS)

endif()

set(LOKA_SCRAPBOOK_SCENARIO_RESOURCE_DIR
  ${CMAKE_CURRENT_BINARY_DIR}/scenario-resources
)
file(MAKE_DIRECTORY ${LOKA_SCRAPBOOK_SCENARIO_RESOURCE_DIR})
configure_file(
  ${PROJECT_SOURCE_DIR}/example/ScrapbookUI/assets/ASSETS-modern.LRP
  ${LOKA_SCRAPBOOK_SCENARIO_RESOURCE_DIR}/ASSETS.LRP
  COPYONLY
)
set_source_files_properties(
  ${LOKA_SCRAPBOOK_SCENARIO_RESOURCE_DIR}/ASSETS.LRP
  PROPERTIES
    GENERATED TRUE
    MACOSX_PACKAGE_LOCATION Resources
)

function(loka_configure_scenario_test_bundle target bundle_id example_include_dir)
  loka_enable_warnings(${target})
  target_include_directories(${target} PRIVATE
    ${PROJECT_SOURCE_DIR}/common
    ${PROJECT_SOURCE_DIR}/tests/macos
    ${PROJECT_SOURCE_DIR}/tests/scenarios
    ${PROJECT_SOURCE_DIR}/tests/standalone
    ${PROJECT_SOURCE_DIR}/${example_include_dir}
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/src/context
  )
  target_compile_definitions(${target} PRIVATE TEST_BUILD)
  target_compile_options(${target} PRIVATE -fno-exceptions)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND LOKA_MACOS_EXPLICIT_NO_ARC_FLAGS)
    target_compile_options(${target} PRIVATE "-fno-objc-arc")
  endif()
  target_link_libraries(${target} PRIVATE LokaAppleMacOSAppKit)
  set_target_properties(${target} PROPERTIES
    CXX_STANDARD 98
    CXX_STANDARD_REQUIRED YES
    OBJCXX_STANDARD 98
    OBJCXX_STANDARD_REQUIRED YES
    MACOSX_BUNDLE_BUNDLE_NAME "${target}"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${bundle_id}"
  )
endfunction()

if(NOT LOKA_MAC_OS_10_4)
  set(LOKA_MACOS_SCENARIO_COMMON_SOURCES
    ${PROJECT_SOURCE_DIR}/tests/macos/ScenarioDriverSupport.mm
    ${PROJECT_SOURCE_DIR}/tests/scenarios/ScenarioProfile.cpp
    ${PROJECT_SOURCE_DIR}/tests/scenarios/ScenarioTypes.cpp
    ${PROJECT_SOURCE_DIR}/tests/scenarios/StartupScenarios.cpp
    ${PROJECT_SOURCE_DIR}/tests/scenarios/RectSurfaceScenarioObservation.cpp
    ${PROJECT_SOURCE_DIR}/tests/TestingHooks.cpp
    ${LOKA_MACOS_CORE_SOURCES}
    ${COMMON_SOURCES}
    ${COMMON_HEADERS}
    ${LOKA_MACOS_CORE_HEADERS}
  )

  function(loka_add_macos_scenario_vehicle_bundle
           target driver bundle_id example_include_dir common_sources)
    # TEST-only vertical slices compile common and platform sources with one
    # uniform TEST_BUILD definition rather than linking the shipping core.
    add_executable(${target} EXCLUDE_FROM_ALL MACOSX_BUNDLE
      ${PROJECT_SOURCE_DIR}/tests/macos/${driver}
      ${${common_sources}}
      ${ARGN}
    )
    loka_configure_scenario_test_bundle(
      ${target}
      "${bundle_id}"
      "${example_include_dir}"
    )
  endfunction()

  function(loka_add_macos_scenario_bundle target driver bundle_id example_include_dir)
    loka_add_macos_scenario_vehicle_bundle(
      ${target}
      ${driver}
      ${bundle_id}
      ${example_include_dir}
      LOKA_MACOS_SCENARIO_COMMON_SOURCES
      ${ARGN}
    )
  endfunction()

  loka_add_macos_scenario_bundle(
    LokaScrapbookScenarioMacOS
    ScenarioDriver.mm
    com.cubenoy22.loka.scrapbook-scenario
    example/ScrapbookUI/src
    ${PROJECT_SOURCE_DIR}/tests/scenarios/ScrapbookScenarios.cpp
    ${PROJECT_SOURCE_DIR}/example/ScrapbookUI/src/ScrapbookPackage.cpp
    ${LOKA_SCRAPBOOK_SCENARIO_RESOURCE_DIR}/ASSETS.LRP
  )
  loka_add_macos_scenario_bundle(
    LokaHelloWorldScenarioMacOS
    HelloWorldScenarioDriver.mm
    com.cubenoy22.loka.helloworld-scenario
    example/HelloWorld/src
    ${PROJECT_SOURCE_DIR}/tests/scenarios/HelloWorldScenarios.cpp
    ${PROJECT_SOURCE_DIR}/example/HelloWorld/src/MainNode.cpp
  )
  loka_add_macos_scenario_bundle(
    LokaTutorialScenarioMacOS
    TutorialScenarioDriver.mm
    com.cubenoy22.loka.tutorial-scenario
    example/Tutorial/src
    ${PROJECT_SOURCE_DIR}/tests/scenarios/TutorialScenarios.cpp
  )
  loka_add_macos_scenario_bundle(
    LokaMineSweeperScenarioMacOS
    MineSweeperScenarioDriver.mm
    com.cubenoy22.loka.minesweeper-scenario
    example/MineSweeper/src
    ${PROJECT_SOURCE_DIR}/tests/scenarios/MineSweeperScenarios.cpp
  )
  loka_add_macos_scenario_bundle(
    LokaFloppyBirdScenarioMacOS
    FloppyBirdScenarioDriver.mm
    com.cubenoy22.loka.floppybird-scenario
    example/FloppyBird/src
    ${PROJECT_SOURCE_DIR}/tests/scenarios/FloppyBirdScenarios.cpp
  )

  loka_add_macos_scenario_bundle(
    LokaSmirkBenchScenarioMacOS
    SmirkBenchScenarioDriver.mm
    com.cubenoy22.loka.smirkbench-scenario
    example/SmirkBench/src
  )

  # Human-facing loop reels share the presentation and scenario rails, but not
  # the host-controlled ScenarioDriverSupport.mm protocol implementation.
  set(LOKA_MACOS_SCENARIO_LOOP_COMMON_SOURCES
    ${PROJECT_SOURCE_DIR}/tests/scenarios/ScenarioReel.cpp
    ${PROJECT_SOURCE_DIR}/tests/scenarios/ScenarioTypes.cpp
    ${PROJECT_SOURCE_DIR}/tests/scenarios/StartupScenarios.cpp
    ${PROJECT_SOURCE_DIR}/tests/scenarios/RectSurfaceScenarioObservation.cpp
    ${PROJECT_SOURCE_DIR}/tests/TestingHooks.cpp
    ${LOKA_MACOS_CORE_SOURCES}
    ${COMMON_SOURCES}
    ${COMMON_HEADERS}
    ${LOKA_MACOS_CORE_HEADERS}
  )

  function(loka_add_macos_scenario_loop_bundle target driver bundle_id example_include_dir)
    loka_add_macos_scenario_vehicle_bundle(
      ${target}
      ${driver}
      ${bundle_id}
      ${example_include_dir}
      LOKA_MACOS_SCENARIO_LOOP_COMMON_SOURCES
      ${ARGN}
    )
  endfunction()

  loka_add_macos_scenario_loop_bundle(
    LokaHelloWorldScenarioLoopMacOS
    HelloWorldScenarioLoop.mm
    com.cubenoy22.loka.helloworld-scenario-loop
    example/HelloWorld/src
    ${PROJECT_SOURCE_DIR}/tests/scenarios/HelloWorldScenarios.cpp
    ${PROJECT_SOURCE_DIR}/example/HelloWorld/src/MainNode.cpp
  )
  loka_add_macos_scenario_loop_bundle(
    LokaMineSweeperScenarioLoopMacOS
    MineSweeperScenarioLoop.mm
    com.cubenoy22.loka.minesweeper-scenario-loop
    example/MineSweeper/src
    ${PROJECT_SOURCE_DIR}/tests/scenarios/MineSweeperScenarios.cpp
  )

  set(LOKA_SCENARIO_LOOP_CYCLES "" CACHE STRING
    "Full reel passes a loop presentation vehicle runs before quitting. Empty means never quit."
  )
  if(NOT LOKA_SCENARIO_LOOP_CYCLES STREQUAL "")
    target_compile_definitions(LokaHelloWorldScenarioLoopMacOS PRIVATE
      LOKA_SCENARIO_LOOP_CYCLES=${LOKA_SCENARIO_LOOP_CYCLES}
    )
    target_compile_definitions(LokaMineSweeperScenarioLoopMacOS PRIVATE
      LOKA_SCENARIO_LOOP_CYCLES=${LOKA_SCENARIO_LOOP_CYCLES}
    )
  endif()
endif()

if(LOKA_ENABLE_STANDALONE_TARGETS)
set(LOKA_STANDALONE_PERFORMANCE_RUNS "" CACHE STRING
  "Complete Standalone Flow passes to measure (3-10). Empty disables measurement."
)

set(LOKA_MACOS_STANDALONE_FLOW_CATALOG
  ${CMAKE_CURRENT_BINARY_DIR}/standalone-flow-catalog.tsv
)
file(WRITE ${LOKA_MACOS_STANDALONE_FLOW_CATALOG}
  "# SimpleViewer is excluded from automation: its file chooser remains interactive.\n"
)
add_custom_target(LokaStandaloneFlowMacOSAll)
add_custom_target(LokaStandaloneLoopMacOSAll)

# Measurement vehicles compile the same TEST_BUILD core as standalone bundles.
add_custom_target(LokaRetirementProbeMacOSAll)
function(loka_add_macos_retirement_probe target driver example_dir)
  add_executable(${target} EXCLUDE_FROM_ALL MACOSX_BUNDLE
    ${PROJECT_SOURCE_DIR}/tests/macos/${driver}
    ${PROJECT_SOURCE_DIR}/tests/macos/RetirementProbeSampler.mm
    ${PROJECT_SOURCE_DIR}/tests/TestingHooks.cpp
    ${LOKA_MACOS_CORE_SOURCES} ${COMMON_SOURCES}
    ${ARGN})
  loka_configure_scenario_test_bundle(${target} "com.cubenoy22.loka.${target}" "${example_dir}")
  target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/tests/probes)
  add_dependencies(LokaRetirementProbeMacOSAll ${target})
endfunction()
loka_add_macos_retirement_probe(LokaRetirementProbeScrapbookMacOS
  RetirementProbeScrapbookMain.mm example/ScrapbookUI/src
  ${PROJECT_SOURCE_DIR}/example/ScrapbookUI/src/ScrapbookPackage.cpp
  ${LOKA_SCRAPBOOK_SCENARIO_RESOURCE_DIR}/ASSETS.LRP)
loka_add_macos_retirement_probe(LokaRetirementProbeLazyMacOS
  RetirementProbeLazyMain.mm tests/probes)
loka_add_macos_retirement_probe(LokaRetirementProbeDialogMacOS
  RetirementProbeDialogMain.mm example/SimpleViewer/src)

function(loka_add_macos_standalone_flow_bundle
         key target loop_target config_header config_type bundle_id example_include_dir expected_audit)
  # Finite targets own one complete App lifetime. Loop targets keep that App
  # and native Window, replacing the scenario rail and re-arming its Scene.
  # The catalog below keeps build, stage, and verification on the same list.
  foreach(_loka_standalone_target IN ITEMS ${target} ${loop_target})
    set(_loka_standalone_bundle_id "${bundle_id}")
    if(_loka_standalone_target STREQUAL loop_target)
      set(_loka_standalone_bundle_id "${bundle_id}.loop")
    endif()
    add_executable(${_loka_standalone_target} EXCLUDE_FROM_ALL MACOSX_BUNDLE
      ${PROJECT_SOURCE_DIR}/tests/macos/StandaloneFlowMain.mm
      ${PROJECT_SOURCE_DIR}/tests/standalone/StandaloneScenarioSupport.cpp
      ${PROJECT_SOURCE_DIR}/tests/scenarios/ScenarioReel.cpp
      ${PROJECT_SOURCE_DIR}/tests/scenarios/ScenarioTypes.cpp
      ${PROJECT_SOURCE_DIR}/tests/TestingHooks.cpp
      ${LOKA_MACOS_CORE_SOURCES}
      ${COMMON_SOURCES}
      ${COMMON_HEADERS}
      ${LOKA_MACOS_CORE_HEADERS}
      ${ARGN}
    )
    loka_configure_scenario_test_bundle(
      ${_loka_standalone_target}
      "${_loka_standalone_bundle_id}"
      "${example_include_dir}"
    )
    target_compile_definitions(${_loka_standalone_target} PRIVATE
      "LOKA_STANDALONE_FLOW_CONFIG_HEADER=\"${config_header}\""
      "LOKA_STANDALONE_FLOW_CONFIG_TYPE=${config_type}"
    )
  endforeach()
  unset(_loka_standalone_bundle_id)
  loka_enable_standalone_performance(
    ${target}
    ${PROJECT_SOURCE_DIR}/tests/standalone/StandalonePerformance.cpp
  )
  target_compile_definitions(${loop_target} PRIVATE LOKA_STANDALONE_FLOW_LOOP=1)
  add_dependencies(LokaStandaloneFlowMacOSAll ${target})
  add_dependencies(LokaStandaloneLoopMacOSAll ${loop_target})
  file(APPEND ${LOKA_MACOS_STANDALONE_FLOW_CATALOG}
    "${key}\t${target}\t${expected_audit}\n"
  )
endfunction()

loka_add_macos_standalone_flow_bundle(
  scrapbook
  LokaScrapbookStandaloneFlowMacOS
  LokaScrapbookStandaloneLoopMacOS
  ScrapbookStandaloneFlowAppConfig.hpp
  ::loka::standalone_tests::ScrapbookStandaloneFlowAppConfig
  com.cubenoy22.loka.scrapbook-standalone-flow
  example/ScrapbookUI/src
  tests/scenarios/expected/scrapbook/standalone-tour.audit
  ${PROJECT_SOURCE_DIR}/tests/standalone/ScrapbookStandaloneFlowAppConfig.cpp
  ${PROJECT_SOURCE_DIR}/tests/scenarios/ScrapbookScenarios.cpp
  ${PROJECT_SOURCE_DIR}/example/ScrapbookUI/src/ScrapbookPackage.cpp
  ${LOKA_SCRAPBOOK_SCENARIO_RESOURCE_DIR}/ASSETS.LRP
)
loka_add_macos_standalone_flow_bundle(
  helloworld
  LokaHelloWorldStandaloneFlowMacOS
  LokaHelloWorldStandaloneLoopMacOS
  HelloWorldStandaloneFlowAppConfig.hpp
  ::loka::standalone_tests::HelloWorldStandaloneFlowAppConfig
  com.cubenoy22.loka.helloworld-standalone-flow
  example/HelloWorld/src
  tests/scenarios/expected/helloworld/toggle-action-probe.audit
  ${PROJECT_SOURCE_DIR}/tests/standalone/HelloWorldStandaloneFlowAppConfig.cpp
  ${PROJECT_SOURCE_DIR}/tests/scenarios/HelloWorldScenarios.cpp
  ${PROJECT_SOURCE_DIR}/example/HelloWorld/src/MainNode.cpp
)
loka_add_macos_standalone_flow_bundle(
  tutorial
  LokaTutorialStandaloneFlowMacOS
  LokaTutorialStandaloneLoopMacOS
  TutorialStandaloneFlowAppConfig.hpp
  ::loka::standalone_tests::TutorialStandaloneFlowAppConfig
  com.cubenoy22.loka.tutorial-standalone-flow
  example/Tutorial/src
  tests/scenarios/expected/tutorial/increment-summary-toggle.audit
  ${PROJECT_SOURCE_DIR}/tests/standalone/TutorialStandaloneFlowAppConfig.cpp
  ${PROJECT_SOURCE_DIR}/tests/scenarios/TutorialScenarios.cpp
)
loka_add_macos_standalone_flow_bundle(
  minesweeper
  LokaMineSweeperStandaloneFlowMacOS
  LokaMineSweeperStandaloneLoopMacOS
  MineSweeperStandaloneFlowAppConfig.hpp
  ::loka::standalone_tests::MineSweeperStandaloneFlowAppConfig
  com.cubenoy22.loka.minesweeper-standalone-flow
  example/MineSweeper/src
  tests/scenarios/expected/minesweeper/new-game-twice.audit
  ${PROJECT_SOURCE_DIR}/tests/standalone/MineSweeperStandaloneFlowAppConfig.cpp
  ${PROJECT_SOURCE_DIR}/tests/scenarios/MineSweeperScenarios.cpp
)
loka_add_macos_standalone_flow_bundle(
  floppybird
  LokaFloppyBirdStandaloneFlowMacOS
  LokaFloppyBirdStandaloneLoopMacOS
  FloppyBirdStandaloneFlowAppConfig.hpp
  ::loka::standalone_tests::FloppyBirdStandaloneFlowAppConfig
  com.cubenoy22.loka.floppybird-standalone-flow
  example/FloppyBird/src
  tests/scenarios/expected/floppybird/fixed-step-flaps.audit
  ${PROJECT_SOURCE_DIR}/tests/standalone/FloppyBirdStandaloneFlowAppConfig.cpp
  ${PROJECT_SOURCE_DIR}/tests/scenarios/FloppyBirdScenarios.cpp
  ${PROJECT_SOURCE_DIR}/tests/scenarios/RectSurfaceScenarioObservation.cpp
)
endif()
