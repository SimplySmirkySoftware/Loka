cmake_minimum_required(VERSION 3.15)
project(LokaTestsToolbox LANGUAGES CXX)

if(NOT COMMAND loka_enable_standalone_performance)
  include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/LokaStandalonePerformance.cmake")
endif()

if(CMAKE_SYSTEM_NAME MATCHES "^Retro(68|PPC)$")
  add_library(LokaToolboxDslNameCollisionProbe OBJECT
    src/DslNameCollisionProbe.cpp
  )
  loka_enable_warnings(LokaToolboxDslNameCollisionProbe)
  # The probe reaches ScrapbookUI's MainNode, whose package header includes
  # the generated <R.hpp>; give it the same shadow-then-src order as the
  # other Scrapbook consumers.
  target_include_directories(LokaToolboxDslNameCollisionProbe BEFORE PRIVATE
    ${LOKA_SCRAPBOOK_R_HEADER_DIR}
  )
  target_include_directories(LokaToolboxDslNameCollisionProbe PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/apple/toolbox/src
    ${CMAKE_SOURCE_DIR}/example/ScrapbookUI/src
  )
  target_compile_definitions(LokaToolboxDslNameCollisionProbe PRIVATE
    LOKA_RETRO68=1
    TEST_BUILD=1
  )
  set_target_properties(LokaToolboxDslNameCollisionProbe PROPERTIES
    CXX_STANDARD 98
    CXX_STANDARD_REQUIRED YES
  )
endif()

if(NOT CMAKE_SYSTEM_NAME MATCHES "^Retro(68|PPC)$")
  add_custom_target(LokaTestsToolbox ALL
    COMMAND ${CMAKE_COMMAND} -E echo
      "LokaTestsToolbox requires a Retro68 Toolbox toolchain. No app is built for this platform."
  )
  return()
endif()

set(LOKA_TESTS_TOOLBOX_TARGET "LokaTestsToolbox${LOKA_CLASSIC_TARGET_SUFFIX}")
set(LOKA_HELLO_WORLD_TESTS_TOOLBOX_TARGET
  "LokaHelloWorldTestsToolbox${LOKA_CLASSIC_TARGET_SUFFIX}"
)
set(LOKA_TUTORIAL_TESTS_TOOLBOX_TARGET
  "LokaTutorialTestsToolbox${LOKA_CLASSIC_TARGET_SUFFIX}"
)
set(LOKA_MINESWEEPER_TESTS_TOOLBOX_TARGET
  "LokaMineSweeperTestsToolbox${LOKA_CLASSIC_TARGET_SUFFIX}"
)
set(LOKA_FLOPPY_BIRD_TESTS_TOOLBOX_TARGET
  "LokaFloppyBirdTestsToolbox${LOKA_CLASSIC_TARGET_SUFFIX}"
)

# The pixel rail reads this clean-build artifact instead of asking the running
# application or reconstructing compiler provenance at verdict time. Retro68
# exposes no stable build-ID query in the supported tree, so the emitter uses
# its installed toolchain content digest as the specified fallback identity.
find_program(LOKA_TOOLBOX_PYTHON3_EXECUTABLE python3 REQUIRED)
set(LOKA_TOOLBOX_BUILD_PROVENANCE
  "${CMAKE_CURRENT_BINARY_DIR}/classic-build-provenance.txt"
)
set(_loka_retro68_toolchain_root "")
if(RETRO68_BUILD_DIR AND EXISTS "${RETRO68_BUILD_DIR}/toolchain")
  set(_loka_retro68_toolchain_root "${RETRO68_BUILD_DIR}/toolchain")
endif()
# This target is deliberately always out of date. Toolchain files can change in
# place without changing their path, and re-emitting this small artifact before
# every dependent Toolbox application prevents stale provenance.
add_custom_target(LokaToolboxBuildProvenance ALL
  COMMAND "${LOKA_TOOLBOX_PYTHON3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/scripts/rig/toolbox/classic_golden_identity.py"
    emit-build-provenance
    --output "${LOKA_TOOLBOX_BUILD_PROVENANCE}"
    --compiler "${CMAKE_CXX_COMPILER}"
    --toolchain-root "${_loka_retro68_toolchain_root}"
  BYPRODUCTS "${LOKA_TOOLBOX_BUILD_PROVENANCE}"
  VERBATIM
)

function(loka_add_toolbox_test_application target core_target)
  add_application(${target} ${ARGN})
  add_dependencies(${target} LokaToolboxBuildProvenance)
  loka_enable_warnings(${target})
  target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/tests/scenarios
    ${CMAKE_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/apple/toolbox/src
  )
  target_link_libraries(${target} PRIVATE ${core_target})
  target_compile_definitions(${target} PRIVATE LOKA_RETRO68=1)
  set_target_properties(${target} PROPERTIES
    CXX_STANDARD 98
    CXX_STANDARD_REQUIRED YES
  )
  target_compile_options(${target} PRIVATE
    -Os
    -ffunction-sections
    -fdata-sections
    -fno-exceptions
    -fno-rtti
  )
  target_link_options(${target} PRIVATE -Wl,--gc-sections)
  # --mac-strip-macsbug belongs to the 68K Elf2Mac linker. RetroPPC emits
  # XCOFF/PEF and rejects that option.
  if(CMAKE_SYSTEM_NAME STREQUAL "Retro68" AND NOT LOKA_RETRO68_DIAGNOSTICS)
    target_link_options(${target} PRIVATE -Wl,--mac-strip-macsbug)
  endif()
endfunction()

# One finite native placement pin; no scenario registry or extra rig cells.
set(LOKA_WINDOW_CLAMP_TARGET "LokaWindowClamp${LOKA_CLASSIC_TARGET_SUFFIX}")
configure_file(
  ${CMAKE_SOURCE_DIR}/example/HelloWorld/Size.r
  ${CMAKE_CURRENT_BINARY_DIR}/WindowClampSize.r
  COPYONLY
)
loka_add_toolbox_test_application(
  ${LOKA_WINDOW_CLAMP_TARGET}
  LokaAppleToolboxTestCore
  src/WindowClampStandaloneMain.cpp
  ${CMAKE_SOURCE_DIR}/tests/TestingHooks.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/WindowClampSize.r
)
target_include_directories(${LOKA_WINDOW_CLAMP_TARGET} PRIVATE
  ${CMAKE_SOURCE_DIR}/tests/standalone
)
target_compile_definitions(${LOKA_WINDOW_CLAMP_TARGET} PRIVATE TEST_BUILD LOKA_DIAG=1)
set_property(TARGET ${LOKA_WINDOW_CLAMP_TARGET} PROPERTY EXCLUDE_FROM_ALL TRUE)
set_property(TARGET ${LOKA_WINDOW_CLAMP_TARGET}_APPL PROPERTY EXCLUDE_FROM_ALL TRUE)

# One finite native activation pin; same shape and SIZE as the clamp pin.
set(LOKA_WINDOW_ACTIVATION_TARGET "LokaWindowActivation${LOKA_CLASSIC_TARGET_SUFFIX}")
loka_add_toolbox_test_application(
  ${LOKA_WINDOW_ACTIVATION_TARGET}
  LokaAppleToolboxTestCore
  src/WindowActivationStandaloneMain.cpp
  ${CMAKE_SOURCE_DIR}/tests/TestingHooks.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/WindowClampSize.r
)
target_include_directories(${LOKA_WINDOW_ACTIVATION_TARGET} PRIVATE
  ${CMAKE_SOURCE_DIR}/tests/standalone
)
target_compile_definitions(${LOKA_WINDOW_ACTIVATION_TARGET} PRIVATE TEST_BUILD LOKA_DIAG=1)
set_property(TARGET ${LOKA_WINDOW_ACTIVATION_TARGET} PROPERTY EXCLUDE_FROM_ALL TRUE)
set_property(TARGET ${LOKA_WINDOW_ACTIVATION_TARGET}_APPL PROPERTY EXCLUDE_FROM_ALL TRUE)

# Production runner cost, with no TEST_BUILD trace or native editing in the interval.
set(LOKA_SETTLE_PROBE_TARGET "LokaSettleProbe${LOKA_CLASSIC_TARGET_SUFFIX}")
loka_add_toolbox_test_application(
  ${LOKA_SETTLE_PROBE_TARGET}
  LokaAppleToolboxCore
  src/SettleProbeStandaloneMain.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/WindowClampSize.r
)
target_include_directories(${LOKA_SETTLE_PROBE_TARGET} PRIVATE ${CMAKE_SOURCE_DIR}/tests)
set_property(TARGET ${LOKA_SETTLE_PROBE_TARGET} PROPERTY EXCLUDE_FROM_ALL TRUE)
set_property(TARGET ${LOKA_SETTLE_PROBE_TARGET}_APPL PROPERTY EXCLUDE_FROM_ALL TRUE)

# Value-only #827 cost probes; same 512K preferred / 384K minimum as AppRecycle.
configure_file(
  ${CMAKE_SOURCE_DIR}/example/HelloWorld/Size.r
  ${CMAKE_CURRENT_BINARY_DIR}/AttributedStringProbeSize.r
  COPYONLY
)
# HFS file names are at most 31 characters, so the Diag twin keeps its name short
# enough for hcopy to stage the MacBinary (LokaAttributedStringProbeDiag68K is 32).
function(loka_add_attributed_string_probe name core_target)
  set(target "${name}${LOKA_CLASSIC_TARGET_SUFFIX}")
  loka_add_toolbox_test_application(
    ${target}
    ${core_target}
    src/AttributedStringProbeStandaloneMain.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/AttributedStringProbeSize.r
  )
  target_include_directories(${target} PRIVATE
    ${CMAKE_SOURCE_DIR}/tests/standalone
  )
  # Match header definitions to the linked core. Census accounting is only
  # useful in the diagnostic twin; it must not contaminate the timing build.
  if(core_target STREQUAL "LokaAppleToolboxTestCore")
    target_sources(${target} PRIVATE ${CMAKE_SOURCE_DIR}/tests/TestingHooks.cpp)
    target_compile_definitions(${target} PRIVATE TEST_BUILD LOKA_DIAG=1)
  endif()
  set_property(TARGET ${target} PROPERTY EXCLUDE_FROM_ALL TRUE)
  set_property(TARGET ${target}_APPL PROPERTY EXCLUDE_FROM_ALL TRUE)
endfunction()
loka_add_attributed_string_probe(LokaAttributedStringProbe LokaAppleToolboxCore)
loka_add_attributed_string_probe(LokaAttrStringProbeDiag LokaAppleToolboxTestCore)

# Rally 7 resident/lazy paragraph timing probe; test-only and opt-in.
set(LOKA_PROBE_SERIES "20,50,-50,100,-200,200" CACHE STRING "Signed paragraph counts: positive S1, negative S2")
function(loka_add_paragraph_probe)
  set(target "LokaParagraphProbe${LOKA_CLASSIC_TARGET_SUFFIX}")
  loka_add_toolbox_test_application(${target} LokaAppleToolboxCore
    src/ParagraphProbeStandaloneMain.cpp ParagraphProbeSize.r)
  target_compile_definitions(${target} PRIVATE "LOKA_PROBE_SERIES=${LOKA_PROBE_SERIES}")
  set_property(TARGET ${target} PROPERTY EXCLUDE_FROM_ALL TRUE)
  set_property(TARGET ${target}_APPL PROPERTY EXCLUDE_FROM_ALL TRUE)
endfunction()
loka_add_paragraph_probe()

# Deliberate timing-only twin of the attributed-string probe registration above.
configure_file(
  ${CMAKE_SOURCE_DIR}/example/HelloWorld/Size.r
  ${CMAKE_CURRENT_BINARY_DIR}/TEStyleProbeSize.r
  COPYONLY
)
# HFS names stay below 31 characters, including _APPL and .bin (28 total).
function(loka_add_te_style_probe name core_target)
  set(target "${name}${LOKA_CLASSIC_TARGET_SUFFIX}")
  loka_add_toolbox_test_application(
    ${target}
    ${core_target}
    src/TEStyleProbeStandaloneMain.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/TEStyleProbeSize.r
  )
  target_include_directories(${target} PRIVATE
    ${CMAKE_SOURCE_DIR}/tests/standalone
  )
  set_property(TARGET ${target} PROPERTY EXCLUDE_FROM_ALL TRUE)
  set_property(TARGET ${target}_APPL PROPERTY EXCLUDE_FROM_ALL TRUE)
endfunction()
loka_add_te_style_probe(LokaTEStyleProbe LokaAppleToolboxCore)

# Frost probe (decoration rally 2 page 5); probe branch only, not for merge.
configure_file(
  ${CMAKE_SOURCE_DIR}/example/HelloWorld/Size.r
  ${CMAKE_CURRENT_BINARY_DIR}/FrostProbeSize.r
  COPYONLY
)
function(loka_add_frost_probe name core_target)
  set(target "${name}${LOKA_CLASSIC_TARGET_SUFFIX}")
  loka_add_toolbox_test_application(
    ${target}
    ${core_target}
    src/FrostProbeStandaloneMain.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/FrostProbeSize.r
  )
  if(LOKA_FROST_PROBE_CPU)
    target_compile_options(${target} PRIVATE "-m${LOKA_FROST_PROBE_CPU}")
  endif()
  set_property(TARGET ${target} PROPERTY EXCLUDE_FROM_ALL TRUE)
  set_property(TARGET ${target}_APPL PROPERTY EXCLUDE_FROM_ALL TRUE)
endfunction()
loka_add_frost_probe(LokaFrostProbe LokaAppleToolboxCore)

# Interactive editor; production core, no scenario driver or idle callback.
# LokaTextEditor68K_APPL.bin is 26 characters (HFS limit: 31).
set(LOKA_TEXT_EDITOR_TARGET "LokaTextEditor${LOKA_CLASSIC_TARGET_SUFFIX}")
configure_file(
  ${CMAKE_SOURCE_DIR}/example/HelloWorld/Size.r
  ${CMAKE_CURRENT_BINARY_DIR}/TextEditorSize.r
  COPYONLY
)
loka_add_toolbox_test_application(
  ${LOKA_TEXT_EDITOR_TARGET}
  LokaAppleToolboxCore
  src/TextEditorStandaloneMain.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/TextEditorSize.r
)
set_property(TARGET ${LOKA_TEXT_EDITOR_TARGET} PROPERTY EXCLUDE_FROM_ALL TRUE)
set_property(TARGET ${LOKA_TEXT_EDITOR_TARGET}_APPL PROPERTY EXCLUDE_FROM_ALL TRUE)

set(LOKA_PAINT_DAMAGE_TARGET "LokaPaintDamage${LOKA_CLASSIC_TARGET_SUFFIX}")
loka_add_toolbox_test_application(
  ${LOKA_PAINT_DAMAGE_TARGET}
  LokaAppleToolboxTestCore
  src/PaintDamageStandaloneMain.cpp
  ${CMAKE_SOURCE_DIR}/tests/TestingHooks.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/PaintDamageSize.r
)
target_include_directories(${LOKA_PAINT_DAMAGE_TARGET} PRIVATE
  ${CMAKE_SOURCE_DIR}/tests/standalone
  ${CMAKE_SOURCE_DIR}/tests/scenarios
)
target_compile_definitions(${LOKA_PAINT_DAMAGE_TARGET} PRIVATE TEST_BUILD LOKA_DIAG=1)
set_property(TARGET ${LOKA_PAINT_DAMAGE_TARGET} PROPERTY EXCLUDE_FROM_ALL TRUE)
set_property(TARGET ${LOKA_PAINT_DAMAGE_TARGET}_APPL PROPERTY EXCLUDE_FROM_ALL TRUE)

set(LOKA_TESTS_TOOLBOX_SOURCES
  src/main.cpp
  src/ScenarioDriver.cpp
  src/ScenarioDriverSupport.cpp
  ${CMAKE_SOURCE_DIR}/tests/scenarios/ScenarioTypes.cpp
  ${CMAKE_SOURCE_DIR}/tests/scenarios/ScrapbookScenarios.cpp
  ${CMAKE_SOURCE_DIR}/example/ScrapbookUI/src/ScrapbookPackage.cpp
  ${CMAKE_SOURCE_DIR}/common/testing/snap/SnapFormat.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/ScrapbookVehicleSize.r
)

loka_add_toolbox_test_application(
  ${LOKA_TESTS_TOOLBOX_TARGET}
  LokaAppleToolboxCore
  ${LOKA_TESTS_TOOLBOX_SOURCES}
)
# The registry vehicle compiles ScrapbookUI's package sources, which include
# the generated <R.hpp>; shadow dir first so a regenerated header overrides
# the checked-in one.
target_include_directories(${LOKA_TESTS_TOOLBOX_TARGET} BEFORE PRIVATE
  ${LOKA_SCRAPBOOK_R_HEADER_DIR}
)
target_include_directories(${LOKA_TESTS_TOOLBOX_TARGET} PRIVATE
  ${CMAKE_SOURCE_DIR}/example/ScrapbookUI/src
)

set(LOKA_HELLO_WORLD_TESTS_TOOLBOX_SOURCES
  src/HelloWorldScenarioMain.cpp
  src/HelloWorldScenarioDriver.cpp
  src/ScenarioDriverSupport.cpp
  ${CMAKE_SOURCE_DIR}/tests/TestingHooks.cpp
  ${CMAKE_SOURCE_DIR}/tests/scenarios/ScenarioTypes.cpp
  ${CMAKE_SOURCE_DIR}/tests/scenarios/HelloWorldScenarios.cpp
  ${CMAKE_SOURCE_DIR}/tests/scenarios/StartupScenarios.cpp
  ${CMAKE_SOURCE_DIR}/tests/scenarios/RectSurfaceScenarioObservation.cpp
  ${CMAKE_SOURCE_DIR}/example/HelloWorld/src/MainNode.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/HelloWorldSize.r
)
configure_file(
  ${CMAKE_SOURCE_DIR}/example/HelloWorld/Size.r
  ${CMAKE_CURRENT_BINARY_DIR}/HelloWorldSize.r
  COPYONLY
)
loka_add_toolbox_test_application(
  ${LOKA_HELLO_WORLD_TESTS_TOOLBOX_TARGET}
  LokaAppleToolboxTestCore
  ${LOKA_HELLO_WORLD_TESTS_TOOLBOX_SOURCES}
)
target_include_directories(${LOKA_HELLO_WORLD_TESTS_TOOLBOX_TARGET} PRIVATE
  ${CMAKE_SOURCE_DIR}/example/HelloWorld/src
)
target_compile_definitions(${LOKA_HELLO_WORLD_TESTS_TOOLBOX_TARGET} PRIVATE
  TEST_BUILD
  LOKA_DIAG=1
)

set(LOKA_TUTORIAL_TESTS_TOOLBOX_SOURCES
  src/TutorialScenarioMain.cpp
  src/TutorialScenarioDriver.cpp
  src/ScenarioDriverSupport.cpp
  ${CMAKE_SOURCE_DIR}/tests/TestingHooks.cpp
  ${CMAKE_SOURCE_DIR}/tests/scenarios/ScenarioTypes.cpp
  ${CMAKE_SOURCE_DIR}/tests/scenarios/TutorialScenarios.cpp
  ${CMAKE_SOURCE_DIR}/tests/scenarios/StartupScenarios.cpp
  ${CMAKE_SOURCE_DIR}/tests/scenarios/RectSurfaceScenarioObservation.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/TutorialSize.r
)
configure_file(
  ${CMAKE_SOURCE_DIR}/example/Tutorial/Size.r
  ${CMAKE_CURRENT_BINARY_DIR}/TutorialSize.r
  COPYONLY
)
loka_add_toolbox_test_application(
  ${LOKA_TUTORIAL_TESTS_TOOLBOX_TARGET}
  LokaAppleToolboxTestCore
  ${LOKA_TUTORIAL_TESTS_TOOLBOX_SOURCES}
)
target_include_directories(${LOKA_TUTORIAL_TESTS_TOOLBOX_TARGET} PRIVATE
  ${CMAKE_SOURCE_DIR}/example/Tutorial/src
)
target_compile_definitions(${LOKA_TUTORIAL_TESTS_TOOLBOX_TARGET} PRIVATE
  TEST_BUILD
  LOKA_DIAG=1
)

set(LOKA_MINESWEEPER_TESTS_TOOLBOX_SOURCES
  src/MineSweeperScenarioMain.cpp
  src/MineSweeperScenarioDriver.cpp
  src/ScenarioDriverSupport.cpp
  ${CMAKE_SOURCE_DIR}/tests/TestingHooks.cpp
  ${CMAKE_SOURCE_DIR}/tests/scenarios/ScenarioTypes.cpp
  ${CMAKE_SOURCE_DIR}/tests/scenarios/MineSweeperScenarios.cpp
  ${CMAKE_SOURCE_DIR}/tests/scenarios/StartupScenarios.cpp
  ${CMAKE_SOURCE_DIR}/tests/scenarios/RectSurfaceScenarioObservation.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/MineSweeperScenarioSize.r
)
loka_add_toolbox_test_application(
  ${LOKA_MINESWEEPER_TESTS_TOOLBOX_TARGET}
  LokaAppleToolboxTestCore
  ${LOKA_MINESWEEPER_TESTS_TOOLBOX_SOURCES}
)
target_include_directories(${LOKA_MINESWEEPER_TESTS_TOOLBOX_TARGET} PRIVATE
  ${CMAKE_SOURCE_DIR}/example/MineSweeper/src
)
target_compile_definitions(${LOKA_MINESWEEPER_TESTS_TOOLBOX_TARGET} PRIVATE
  TEST_BUILD
  LOKA_DIAG=1
)

set(LOKA_FLOPPY_BIRD_TESTS_TOOLBOX_SOURCES
  src/FloppyBirdScenarioMain.cpp
  src/FloppyBirdScenarioDriver.cpp
  src/ScenarioDriverSupport.cpp
  ${CMAKE_SOURCE_DIR}/tests/TestingHooks.cpp
  ${CMAKE_SOURCE_DIR}/tests/scenarios/ScenarioTypes.cpp
  ${CMAKE_SOURCE_DIR}/tests/scenarios/FloppyBirdScenarios.cpp
  ${CMAKE_SOURCE_DIR}/tests/scenarios/StartupScenarios.cpp
  ${CMAKE_SOURCE_DIR}/tests/scenarios/RectSurfaceScenarioObservation.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/FloppyBirdSize.r
)
configure_file(
  ${CMAKE_SOURCE_DIR}/example/FloppyBird/Size.r
  ${CMAKE_CURRENT_BINARY_DIR}/FloppyBirdSize.r
  COPYONLY
)
loka_add_toolbox_test_application(
  ${LOKA_FLOPPY_BIRD_TESTS_TOOLBOX_TARGET}
  LokaAppleToolboxTestCore
  ${LOKA_FLOPPY_BIRD_TESTS_TOOLBOX_SOURCES}
)
target_include_directories(${LOKA_FLOPPY_BIRD_TESTS_TOOLBOX_TARGET} PRIVATE
  ${CMAKE_SOURCE_DIR}/example/FloppyBird/src
)
target_compile_definitions(${LOKA_FLOPPY_BIRD_TESTS_TOOLBOX_TARGET} PRIVATE
  TEST_BUILD
  LOKA_DIAG=1
)

set(LOKA_SMIRK_BENCH_TESTS_TOOLBOX_TARGET
  "LokaSmirkBenchTestsToolbox${LOKA_CLASSIC_TARGET_SUFFIX}"
)
configure_file(
  ${CMAKE_SOURCE_DIR}/example/SmirkBench/Size.r
  ${CMAKE_CURRENT_BINARY_DIR}/SmirkBenchSize.r
  COPYONLY
)
loka_add_toolbox_test_application(
  ${LOKA_SMIRK_BENCH_TESTS_TOOLBOX_TARGET}
  LokaAppleToolboxTestCore
  src/SmirkBenchScenarioMain.cpp
  src/SmirkBenchScenarioDriver.cpp
  src/ScenarioDriverSupport.cpp
  ${CMAKE_SOURCE_DIR}/tests/TestingHooks.cpp
  ${CMAKE_SOURCE_DIR}/tests/scenarios/ScenarioTypes.cpp
  ${CMAKE_SOURCE_DIR}/tests/scenarios/RectSurfaceScenarioObservation.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/SmirkBenchSize.r
)
target_include_directories(${LOKA_SMIRK_BENCH_TESTS_TOOLBOX_TARGET} PRIVATE
  ${CMAKE_SOURCE_DIR}/example/SmirkBench/src
)
target_compile_definitions(${LOKA_SMIRK_BENCH_TESTS_TOOLBOX_TARGET} PRIVATE TEST_BUILD LOKA_DIAG=1)

set(LOKA_SIMPLE_VIEWER_TESTS_TOOLBOX_TARGET
  "LokaSimpleViewerTestsToolbox${LOKA_CLASSIC_TARGET_SUFFIX}"
)
configure_file(
  ${CMAKE_SOURCE_DIR}/example/SimpleViewer/Size.r
  ${CMAKE_CURRENT_BINARY_DIR}/SimpleViewerSize.r
  COPYONLY
)
loka_add_toolbox_test_application(
  ${LOKA_SIMPLE_VIEWER_TESTS_TOOLBOX_TARGET}
  LokaAppleToolboxTestCore
  src/SimpleViewerScenarioMain.cpp
  src/SimpleViewerScenarioDriver.cpp
  src/ScenarioDriverSupport.cpp
  ${CMAKE_SOURCE_DIR}/tests/TestingHooks.cpp
  ${CMAKE_SOURCE_DIR}/tests/scenarios/ScenarioTypes.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/SimpleViewerSize.r
)
target_include_directories(${LOKA_SIMPLE_VIEWER_TESTS_TOOLBOX_TARGET} PRIVATE
  ${CMAKE_SOURCE_DIR}/example/SimpleViewer/src
)
target_compile_definitions(${LOKA_SIMPLE_VIEWER_TESTS_TOOLBOX_TARGET} PRIVATE TEST_BUILD LOKA_DIAG=1)

# LokaSimpleTextTest68K_APPL.bin is 30 characters; HFS permits 31.
set(LOKA_SIMPLE_TEXT_TESTS_TOOLBOX_TARGET
  "LokaSimpleTextTest${LOKA_CLASSIC_TARGET_SUFFIX}"
)
configure_file(
  ${CMAKE_SOURCE_DIR}/example/SimpleText/Size.r
  ${CMAKE_CURRENT_BINARY_DIR}/SimpleTextSize.r
  COPYONLY
)
loka_add_toolbox_test_application(
  ${LOKA_SIMPLE_TEXT_TESTS_TOOLBOX_TARGET}
  LokaAppleToolboxTestCore
  src/SimpleTextScenarioMain.cpp
  src/SimpleTextScenarioDriver.cpp
  src/ScenarioDriverSupport.cpp
  ${CMAKE_SOURCE_DIR}/tests/TestingHooks.cpp
  ${CMAKE_SOURCE_DIR}/tests/scenarios/ScenarioTypes.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/SimpleTextSize.r
)
target_include_directories(${LOKA_SIMPLE_TEXT_TESTS_TOOLBOX_TARGET} PRIVATE
  ${CMAKE_SOURCE_DIR}/example/SimpleText/src
)
target_compile_definitions(${LOKA_SIMPLE_TEXT_TESTS_TOOLBOX_TARGET} PRIVATE TEST_BUILD LOKA_DIAG=1)

function(loka_configure_toolbox_standalone_target
         target example_include_dir aggregate_target)
  set_property(TARGET ${target} PROPERTY EXCLUDE_FROM_ALL TRUE)
  set_property(TARGET ${target}_APPL PROPERTY EXCLUDE_FROM_ALL TRUE)
  target_include_directories(${target} PRIVATE
    ${CMAKE_SOURCE_DIR}/tests/standalone
    ${example_include_dir}
  )
  target_compile_definitions(${target} PRIVATE
    TEST_BUILD
    LOKA_DIAG=1
  )
  # The aggregates exist only with LOKA_ENABLE_STANDALONE_TARGETS.
  if(TARGET ${aggregate_target})
    add_dependencies(${aggregate_target} ${target}_APPL)
  endif()
endfunction()

if(LOKA_ENABLE_STANDALONE_TARGETS)
set(LOKA_STANDALONE_FLOW_TOOLBOX_TARGET
  # Keep the generated application name within Classic HFS's 31-character limit.
  "LokaScrapbookStandaloneFlow${LOKA_CLASSIC_TARGET_SUFFIX}"
)

set(LOKA_HELLO_WORLD_STANDALONE_FLOW_TOOLBOX_TARGET
  # Keep the generated application name within Classic HFS's 31-character limit.
  "LokaHelloStandaloneFlow${LOKA_CLASSIC_TARGET_SUFFIX}"
)
set(LOKA_TUTORIAL_STANDALONE_FLOW_TOOLBOX_TARGET
  # Keep the generated application name within Classic HFS's 31-character limit.
  "LokaTutorialStandaloneFlow${LOKA_CLASSIC_TARGET_SUFFIX}"
)
set(LOKA_MINESWEEPER_STANDALONE_FLOW_TOOLBOX_TARGET
  # Keep the generated application name within Classic HFS's 31-character limit.
  "LokaMineStandaloneFlow${LOKA_CLASSIC_TARGET_SUFFIX}"
)
set(LOKA_FLOPPY_BIRD_STANDALONE_FLOW_TOOLBOX_TARGET
  # Keep the generated application name within Classic HFS's 31-character limit.
  "LokaFloppyStandaloneFlow${LOKA_CLASSIC_TARGET_SUFFIX}"
)
set(LOKA_STANDALONE_FLOW_TOOLBOX_SUPPORT_SOURCES
  ${CMAKE_SOURCE_DIR}/tests/standalone/StandaloneScenarioSupport.cpp
  ${CMAKE_SOURCE_DIR}/tests/TestingHooks.cpp
  ${CMAKE_SOURCE_DIR}/tests/scenarios/ScenarioReel.cpp
  ${CMAKE_SOURCE_DIR}/tests/scenarios/ScenarioTypes.cpp
)
set(LOKA_HELLO_WORLD_STANDALONE_FLOW_TOOLBOX_SOURCES
  src/HelloWorldStandaloneFlowMain.cpp
  ${CMAKE_SOURCE_DIR}/tests/standalone/HelloWorldStandaloneFlowApplication.cpp
  ${CMAKE_SOURCE_DIR}/tests/standalone/HelloWorldStandaloneFlowAppConfig.cpp
  ${LOKA_STANDALONE_FLOW_TOOLBOX_SUPPORT_SOURCES}
  ${CMAKE_SOURCE_DIR}/tests/scenarios/HelloWorldScenarios.cpp
  ${CMAKE_SOURCE_DIR}/example/HelloWorld/src/MainNode.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/HelloWorldStandaloneSize.r
)
set(LOKA_TUTORIAL_STANDALONE_FLOW_TOOLBOX_SOURCES
  src/TutorialStandaloneFlowMain.cpp
  ${CMAKE_SOURCE_DIR}/tests/standalone/TutorialStandaloneFlowApplication.cpp
  ${CMAKE_SOURCE_DIR}/tests/standalone/TutorialStandaloneFlowAppConfig.cpp
  ${LOKA_STANDALONE_FLOW_TOOLBOX_SUPPORT_SOURCES}
  ${CMAKE_SOURCE_DIR}/tests/scenarios/TutorialScenarios.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/TutorialStandaloneSize.r
)
set(LOKA_MINESWEEPER_STANDALONE_FLOW_TOOLBOX_SOURCES
  src/MineSweeperStandaloneFlowMain.cpp
  ${CMAKE_SOURCE_DIR}/tests/standalone/MineSweeperStandaloneFlowApplication.cpp
  ${CMAKE_SOURCE_DIR}/tests/standalone/MineSweeperStandaloneFlowAppConfig.cpp
  ${LOKA_STANDALONE_FLOW_TOOLBOX_SUPPORT_SOURCES}
  ${CMAKE_SOURCE_DIR}/tests/scenarios/MineSweeperScenarios.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/MineSweeperStandaloneSize.r
)
set(LOKA_FLOPPY_BIRD_STANDALONE_FLOW_TOOLBOX_SOURCES
  src/FloppyBirdStandaloneFlowMain.cpp
  ${CMAKE_SOURCE_DIR}/tests/standalone/FloppyBirdStandaloneFlowApplication.cpp
  ${CMAKE_SOURCE_DIR}/tests/standalone/FloppyBirdStandaloneFlowAppConfig.cpp
  ${LOKA_STANDALONE_FLOW_TOOLBOX_SUPPORT_SOURCES}
  ${CMAKE_SOURCE_DIR}/tests/scenarios/FloppyBirdScenarios.cpp
  ${CMAKE_SOURCE_DIR}/tests/scenarios/RectSurfaceScenarioObservation.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/FloppyBirdStandaloneSize.r
)
set(LOKA_STANDALONE_FLOW_TOOLBOX_SOURCES
  src/StandaloneFlowMain.cpp
  ${CMAKE_SOURCE_DIR}/tests/standalone/StandaloneFlowApplication.cpp
  ${CMAKE_SOURCE_DIR}/tests/standalone/ScrapbookStandaloneFlowAppConfig.cpp
  ${LOKA_STANDALONE_FLOW_TOOLBOX_SUPPORT_SOURCES}
  ${CMAKE_SOURCE_DIR}/tests/scenarios/ScrapbookScenarios.cpp
  ${CMAKE_SOURCE_DIR}/example/ScrapbookUI/src/ScrapbookPackage.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/ScrapbookVehicleSize.r
)

set(LOKA_STANDALONE_FLOW_TOOLBOX_ALL_TARGET
  "LokaStandaloneFlow${LOKA_CLASSIC_TARGET_SUFFIX}All"
)
set(LOKA_STANDALONE_LOOP_TOOLBOX_ALL_TARGET
  "LokaStandaloneLoop${LOKA_CLASSIC_TARGET_SUFFIX}All"
)
add_custom_target(${LOKA_STANDALONE_FLOW_TOOLBOX_ALL_TARGET})
add_custom_target(${LOKA_STANDALONE_LOOP_TOOLBOX_ALL_TARGET})

loka_add_toolbox_test_application(
  ${LOKA_STANDALONE_FLOW_TOOLBOX_TARGET}
  LokaAppleToolboxTestCore
  ${LOKA_STANDALONE_FLOW_TOOLBOX_SOURCES}
)
loka_configure_toolbox_standalone_target(
  ${LOKA_STANDALONE_FLOW_TOOLBOX_TARGET}
  ${CMAKE_SOURCE_DIR}/example/ScrapbookUI/src
  ${LOKA_STANDALONE_FLOW_TOOLBOX_ALL_TARGET}
)

# The standalone helper above already supplies tests/standalone, the example
# src, and the TEST_BUILD defines; only the generated-header shadow directory
# is Scrapbook-specific (it must precede the src include so a regenerated
# R.hpp overrides the checked-in one, as on the tests target above).
target_include_directories(${LOKA_STANDALONE_FLOW_TOOLBOX_TARGET} BEFORE PRIVATE
  ${LOKA_SCRAPBOOK_R_HEADER_DIR}
)


configure_file(
  ${CMAKE_SOURCE_DIR}/example/HelloWorld/Size.r
  ${CMAKE_CURRENT_BINARY_DIR}/HelloWorldStandaloneSize.r
  COPYONLY
)
loka_add_toolbox_test_application(
  ${LOKA_HELLO_WORLD_STANDALONE_FLOW_TOOLBOX_TARGET}
  LokaAppleToolboxTestCore
  ${LOKA_HELLO_WORLD_STANDALONE_FLOW_TOOLBOX_SOURCES}
)
loka_configure_toolbox_standalone_target(
  ${LOKA_HELLO_WORLD_STANDALONE_FLOW_TOOLBOX_TARGET}
  ${CMAKE_SOURCE_DIR}/example/HelloWorld/src
  ${LOKA_STANDALONE_FLOW_TOOLBOX_ALL_TARGET}
)

# Eight fresh menu-bearing App lifetimes in one process (#406).
set(LOKA_APP_RECYCLE_TARGET "LokaAppRecycle${LOKA_CLASSIC_TARGET_SUFFIX}")
loka_add_toolbox_test_application(
  ${LOKA_APP_RECYCLE_TARGET}
  LokaAppleToolboxTestCore
  src/AppRecycleStandaloneMain.cpp
  ${CMAKE_SOURCE_DIR}/tests/standalone/HelloWorldStandaloneFlowAppConfig.cpp
  ${CMAKE_SOURCE_DIR}/tests/standalone/StandalonePerformance.cpp
  ${LOKA_STANDALONE_FLOW_TOOLBOX_SUPPORT_SOURCES}
  ${CMAKE_SOURCE_DIR}/tests/scenarios/HelloWorldScenarios.cpp
  ${CMAKE_SOURCE_DIR}/example/HelloWorld/src/MainNode.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/HelloWorldStandaloneSize.r
)
target_include_directories(${LOKA_APP_RECYCLE_TARGET} PRIVATE
  ${CMAKE_SOURCE_DIR}/tests/standalone
  ${CMAKE_SOURCE_DIR}/example/HelloWorld/src
)
target_compile_definitions(${LOKA_APP_RECYCLE_TARGET} PRIVATE
  TEST_BUILD LOKA_DIAG=1 LOKA_STANDALONE_PERFORMANCE_RUNS=8
)
set_property(TARGET ${LOKA_APP_RECYCLE_TARGET} PROPERTY EXCLUDE_FROM_ALL TRUE)
set_property(TARGET ${LOKA_APP_RECYCLE_TARGET}_APPL PROPERTY EXCLUDE_FROM_ALL TRUE)

configure_file(
  ${CMAKE_SOURCE_DIR}/example/Tutorial/Size.r
  ${CMAKE_CURRENT_BINARY_DIR}/TutorialStandaloneSize.r
  COPYONLY
)
loka_add_toolbox_test_application(
  ${LOKA_TUTORIAL_STANDALONE_FLOW_TOOLBOX_TARGET}
  LokaAppleToolboxTestCore
  ${LOKA_TUTORIAL_STANDALONE_FLOW_TOOLBOX_SOURCES}
)
loka_configure_toolbox_standalone_target(
  ${LOKA_TUTORIAL_STANDALONE_FLOW_TOOLBOX_TARGET}
  ${CMAKE_SOURCE_DIR}/example/Tutorial/src
  ${LOKA_STANDALONE_FLOW_TOOLBOX_ALL_TARGET}
)

configure_file(
  ${CMAKE_SOURCE_DIR}/example/MineSweeper/Size.r
  ${CMAKE_CURRENT_BINARY_DIR}/MineSweeperStandaloneSize.r
  COPYONLY
)
loka_add_toolbox_test_application(
  ${LOKA_MINESWEEPER_STANDALONE_FLOW_TOOLBOX_TARGET}
  LokaAppleToolboxTestCore
  ${LOKA_MINESWEEPER_STANDALONE_FLOW_TOOLBOX_SOURCES}
)
loka_configure_toolbox_standalone_target(
  ${LOKA_MINESWEEPER_STANDALONE_FLOW_TOOLBOX_TARGET}
  ${CMAKE_SOURCE_DIR}/example/MineSweeper/src
  ${LOKA_STANDALONE_FLOW_TOOLBOX_ALL_TARGET}
)

configure_file(
  ${CMAKE_SOURCE_DIR}/example/FloppyBird/Size.r
  ${CMAKE_CURRENT_BINARY_DIR}/FloppyBirdStandaloneSize.r
  COPYONLY
)
loka_add_toolbox_test_application(
  ${LOKA_FLOPPY_BIRD_STANDALONE_FLOW_TOOLBOX_TARGET}
  LokaAppleToolboxTestCore
  ${LOKA_FLOPPY_BIRD_STANDALONE_FLOW_TOOLBOX_SOURCES}
)
loka_configure_toolbox_standalone_target(
  ${LOKA_FLOPPY_BIRD_STANDALONE_FLOW_TOOLBOX_TARGET}
  ${CMAKE_SOURCE_DIR}/example/FloppyBird/src
  ${LOKA_STANDALONE_FLOW_TOOLBOX_ALL_TARGET}
)

# Autonomous release loops share the finite standalone vehicles' application
# sources. Only their completion policy differs: one App and native Window stay
# alive while each completed rail is replaced and the current Scene is re-armed.
set(LOKA_STANDALONE_LOOP_TOOLBOX_TARGET
  "LokaScrapbookStandaloneLoop${LOKA_CLASSIC_TARGET_SUFFIX}"
)
set(LOKA_HELLO_WORLD_STANDALONE_LOOP_TOOLBOX_TARGET
  "LokaHelloStandaloneLoop${LOKA_CLASSIC_TARGET_SUFFIX}"
)
set(LOKA_TUTORIAL_STANDALONE_LOOP_TOOLBOX_TARGET
  "LokaTutorialStandaloneLoop${LOKA_CLASSIC_TARGET_SUFFIX}"
)
set(LOKA_MINESWEEPER_STANDALONE_LOOP_TOOLBOX_TARGET
  "LokaMineStandaloneLoop${LOKA_CLASSIC_TARGET_SUFFIX}"
)
set(LOKA_FLOPPY_BIRD_STANDALONE_LOOP_TOOLBOX_TARGET
  "LokaFloppyStandaloneLoop${LOKA_CLASSIC_TARGET_SUFFIX}"
)

function(loka_add_toolbox_standalone_loop target example_include_dir)
  loka_add_toolbox_test_application(
    ${target}
    LokaAppleToolboxTestCore
    ${ARGN}
  )
  loka_configure_toolbox_standalone_target(
    ${target}
    ${example_include_dir}
    ${LOKA_STANDALONE_LOOP_TOOLBOX_ALL_TARGET}
  )
  target_compile_definitions(${target} PRIVATE LOKA_STANDALONE_FLOW_LOOP=1)
endfunction()

loka_add_toolbox_standalone_loop(
  ${LOKA_STANDALONE_LOOP_TOOLBOX_TARGET}
  ${CMAKE_SOURCE_DIR}/example/ScrapbookUI/src
  ${LOKA_STANDALONE_FLOW_TOOLBOX_SOURCES}
)
loka_add_toolbox_standalone_loop(
  ${LOKA_HELLO_WORLD_STANDALONE_LOOP_TOOLBOX_TARGET}
  ${CMAKE_SOURCE_DIR}/example/HelloWorld/src
  ${LOKA_HELLO_WORLD_STANDALONE_FLOW_TOOLBOX_SOURCES}
)
loka_add_toolbox_standalone_loop(
  ${LOKA_TUTORIAL_STANDALONE_LOOP_TOOLBOX_TARGET}
  ${CMAKE_SOURCE_DIR}/example/Tutorial/src
  ${LOKA_TUTORIAL_STANDALONE_FLOW_TOOLBOX_SOURCES}
)

# The previous Toolbox loop soak established that MineSweeper needs more than
# its 512K shipping preference while repeatedly rebuilding the board. Reuse
# that measured loop partition instead of inventing a second memory policy.
set(LOKA_MINESWEEPER_STANDALONE_LOOP_TOOLBOX_SOURCES
  ${LOKA_MINESWEEPER_STANDALONE_FLOW_TOOLBOX_SOURCES}
)
list(REMOVE_ITEM LOKA_MINESWEEPER_STANDALONE_LOOP_TOOLBOX_SOURCES
  ${CMAKE_CURRENT_BINARY_DIR}/MineSweeperStandaloneSize.r
)
list(APPEND LOKA_MINESWEEPER_STANDALONE_LOOP_TOOLBOX_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/ScenarioLoopSize.r
)
loka_add_toolbox_standalone_loop(
  ${LOKA_MINESWEEPER_STANDALONE_LOOP_TOOLBOX_TARGET}
  ${CMAKE_SOURCE_DIR}/example/MineSweeper/src
  ${LOKA_MINESWEEPER_STANDALONE_LOOP_TOOLBOX_SOURCES}
)
loka_add_toolbox_standalone_loop(
  ${LOKA_FLOPPY_BIRD_STANDALONE_LOOP_TOOLBOX_TARGET}
  ${CMAKE_SOURCE_DIR}/example/FloppyBird/src
  ${LOKA_FLOPPY_BIRD_STANDALONE_FLOW_TOOLBOX_SOURCES}
)

add_dependencies(${LOKA_STANDALONE_LOOP_TOOLBOX_ALL_TARGET}
  LokaSimpleViewer${LOKA_CLASSIC_TARGET_SUFFIX}_APPL)

set(LOKA_STANDALONE_PERFORMANCE_RUNS "" CACHE STRING
  "Complete Standalone Flow passes to measure (3-10). Empty disables measurement."
)
foreach(_LOKA_STANDALONE_TARGET IN ITEMS
    ${LOKA_STANDALONE_FLOW_TOOLBOX_TARGET}
    ${LOKA_HELLO_WORLD_STANDALONE_FLOW_TOOLBOX_TARGET}
    ${LOKA_TUTORIAL_STANDALONE_FLOW_TOOLBOX_TARGET}
    ${LOKA_MINESWEEPER_STANDALONE_FLOW_TOOLBOX_TARGET}
    ${LOKA_FLOPPY_BIRD_STANDALONE_FLOW_TOOLBOX_TARGET})
  loka_enable_standalone_performance(
    ${_LOKA_STANDALONE_TARGET}
    ${CMAKE_SOURCE_DIR}/tests/standalone/StandalonePerformance.cpp)
endforeach()
unset(_LOKA_STANDALONE_TARGET)
endif()

configure_file(
  ${CMAKE_SOURCE_DIR}/example/ScrapbookUI/ASSETS.LRP
  ASSETS.LRP
  COPYONLY
)

# Loop presentation vehicles (#402, Classic leg). Same TEST_BUILD /
# EXCLUDE_FROM_ALL shape as the standalone Flow applications: they are
# presentation assets, never part of a default build and never gating.
set(LOKA_MINESWEEPER_SCENARIO_LOOP_TOOLBOX_TARGET
  # Keep the generated application name within Classic HFS's 31-character limit.
  "LokaMineScenarioLoop${LOKA_CLASSIC_TARGET_SUFFIX}"
)
set(LOKA_HELLO_WORLD_SCENARIO_LOOP_TOOLBOX_TARGET
  # Keep the generated application name within Classic HFS's 31-character limit.
  "LokaHelloScenarioLoop${LOKA_CLASSIC_TARGET_SUFFIX}"
)

set(LOKA_MINESWEEPER_SCENARIO_LOOP_TOOLBOX_SOURCES
  src/MineSweeperScenarioLoopMain.cpp
  src/MineSweeperScenarioLoop.cpp
  src/ScenarioDriverSupport.cpp
  ${CMAKE_SOURCE_DIR}/tests/TestingHooks.cpp
  ${CMAKE_SOURCE_DIR}/tests/scenarios/ScenarioTypes.cpp
  ${CMAKE_SOURCE_DIR}/tests/scenarios/ScenarioReel.cpp
  ${CMAKE_SOURCE_DIR}/tests/scenarios/MineSweeperScenarios.cpp
  ${CMAKE_SOURCE_DIR}/tests/scenarios/StartupScenarios.cpp
  ${CMAKE_SOURCE_DIR}/tests/scenarios/RectSurfaceScenarioObservation.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/ScenarioLoopSize.r
)
loka_add_toolbox_test_application(
  ${LOKA_MINESWEEPER_SCENARIO_LOOP_TOOLBOX_TARGET}
  LokaAppleToolboxTestCore
  ${LOKA_MINESWEEPER_SCENARIO_LOOP_TOOLBOX_SOURCES}
)
set_property(TARGET ${LOKA_MINESWEEPER_SCENARIO_LOOP_TOOLBOX_TARGET}
  PROPERTY EXCLUDE_FROM_ALL TRUE
)
set_property(TARGET ${LOKA_MINESWEEPER_SCENARIO_LOOP_TOOLBOX_TARGET}_APPL
  PROPERTY EXCLUDE_FROM_ALL TRUE
)
target_include_directories(${LOKA_MINESWEEPER_SCENARIO_LOOP_TOOLBOX_TARGET} PRIVATE
  ${CMAKE_SOURCE_DIR}/example/MineSweeper/src
)
target_compile_definitions(${LOKA_MINESWEEPER_SCENARIO_LOOP_TOOLBOX_TARGET} PRIVATE
  TEST_BUILD
  LOKA_DIAG=1
)

set(LOKA_HELLO_WORLD_SCENARIO_LOOP_TOOLBOX_SOURCES
  src/HelloWorldScenarioLoopMain.cpp
  src/HelloWorldScenarioLoop.cpp
  src/ScenarioDriverSupport.cpp
  ${CMAKE_SOURCE_DIR}/tests/TestingHooks.cpp
  ${CMAKE_SOURCE_DIR}/tests/scenarios/ScenarioTypes.cpp
  ${CMAKE_SOURCE_DIR}/tests/scenarios/ScenarioReel.cpp
  ${CMAKE_SOURCE_DIR}/tests/scenarios/HelloWorldScenarios.cpp
  ${CMAKE_SOURCE_DIR}/tests/scenarios/StartupScenarios.cpp
  ${CMAKE_SOURCE_DIR}/tests/scenarios/RectSurfaceScenarioObservation.cpp
  ${CMAKE_SOURCE_DIR}/example/HelloWorld/src/MainNode.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/ScenarioLoopSize.r
)
loka_add_toolbox_test_application(
  ${LOKA_HELLO_WORLD_SCENARIO_LOOP_TOOLBOX_TARGET}
  LokaAppleToolboxTestCore
  ${LOKA_HELLO_WORLD_SCENARIO_LOOP_TOOLBOX_SOURCES}
)
set_property(TARGET ${LOKA_HELLO_WORLD_SCENARIO_LOOP_TOOLBOX_TARGET}
  PROPERTY EXCLUDE_FROM_ALL TRUE
)
set_property(TARGET ${LOKA_HELLO_WORLD_SCENARIO_LOOP_TOOLBOX_TARGET}_APPL
  PROPERTY EXCLUDE_FROM_ALL TRUE
)
target_include_directories(${LOKA_HELLO_WORLD_SCENARIO_LOOP_TOOLBOX_TARGET} PRIVATE
  ${CMAKE_SOURCE_DIR}/example/HelloWorld/src
)
target_compile_definitions(${LOKA_HELLO_WORLD_SCENARIO_LOOP_TOOLBOX_TARGET} PRIVATE
  TEST_BUILD
  LOKA_DIAG=1
)

set(LOKA_SCENARIO_LOOP_CYCLES "" CACHE STRING
  "Full reel passes a loop presentation vehicle runs before quitting. Empty means never quit."
)
if(NOT LOKA_SCENARIO_LOOP_CYCLES STREQUAL "")
  # Bounded verification build: the reel quits after N full passes so a MAME
  # run can end on its own. Absent, both vehicles loop forever.
  target_compile_definitions(${LOKA_MINESWEEPER_SCENARIO_LOOP_TOOLBOX_TARGET} PRIVATE
    LOKA_SCENARIO_LOOP_CYCLES=${LOKA_SCENARIO_LOOP_CYCLES}
  )
  target_compile_definitions(${LOKA_HELLO_WORLD_SCENARIO_LOOP_TOOLBOX_TARGET} PRIVATE
    LOKA_SCENARIO_LOOP_CYCLES=${LOKA_SCENARIO_LOOP_CYCLES}
  )
endif()
