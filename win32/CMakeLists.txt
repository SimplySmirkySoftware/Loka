set(CMAKE_CXX_STANDARD 98)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Standalone example projects add this platform directory without the root.
if(NOT COMMAND loka_enable_warnings)
  include("${CMAKE_CURRENT_LIST_DIR}/../cmake/LokaWarnings.cmake")
endif()
if(NOT COMMAND loka_enable_standalone_performance)
  include("${CMAKE_CURRENT_LIST_DIR}/../cmake/LokaStandalonePerformance.cmake")
endif()

file(GLOB_RECURSE COMMON_SOURCES CONFIGURE_DEPENDS ../common/*.cpp)
list(REMOVE_DUPLICATES COMMON_SOURCES)
set(SOURCES
  src/Win32Bootstrap.cpp
  src/Win32App.cpp
  src/Win32MenuAttachment.cpp
  src/Win32BuiltInSupport.cpp
  src/Win32Window.cpp
  src/Win32ScenePlatformController.cpp
  src/Win32PlatformContext.cpp
  src/Win32BitmapCapture.cpp
  src/context/Win32ButtonContext.cpp
  src/context/Win32RetirableContext.cpp
  src/context/Win32OwnedChildWindow.cpp
  src/context/Win32ZStackContext.cpp
  src/Win32CardPainter.cpp
  src/context/Win32CardBandContext.cpp
  src/context/Win32RibbonGroupEdgeContext.cpp
  src/context/Win32CellContext.cpp
  src/context/Win32TextContext.cpp
  src/context/Win32AttributedTextContext.cpp
  src/context/Win32AttributedTextTable.cpp
  src/context/Win32EditTextContext.cpp
  src/context/Win32TextEditorContext.cpp
  src/context/Win32OpenFileDialogContext.cpp
  src/context/Win32ConfirmDialogContext.cpp
  src/context/Win32PopupMenuContext.cpp
  src/context/Win32ImageViewContext.cpp
  src/context/Win32RectSurfaceContext.cpp
  src/context/Win32ScrollViewContext.cpp
  src/platform/Win32DebugLog.cpp
  src/platform/Win32Profiler.cpp
  src/platform/Win32String.cpp
  src/platform/Win32FileIO.cpp
  src/platform/Win32AppLocation.cpp
  src/platform/Win32IdlePacer.cpp
  src/platform/Win32DisplayScale.cpp
  src/platform/Win32DisplayFont.cpp
)

add_library(LokaWin32WideApiContract INTERFACE)
target_compile_definitions(LokaWin32WideApiContract INTERFACE
  UNICODE
  _UNICODE
)

add_library(LokaWin32Core STATIC ${SOURCES} ${COMMON_SOURCES})
loka_enable_warnings(LokaWin32Core)
target_include_directories(LokaWin32Core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/../common
)

target_include_directories(LokaWin32Core PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(LokaWin32Core
  PUBLIC LokaWin32WideApiContract
  PRIVATE windowscodecs ole32
)

if(MSVC)
    target_compile_options(LokaWin32Core PRIVATE /utf-8)
endif()

get_filename_component(LOKA_WIN32_REPOSITORY_ROOT
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ABSOLUTE
)
string(TOLOWER "${CMAKE_SOURCE_DIR}" LOKA_WIN32_TOP_LEVEL_SOURCE_NORMALIZED)
string(TOLOWER "${LOKA_WIN32_REPOSITORY_ROOT}" LOKA_WIN32_REPOSITORY_ROOT_NORMALIZED)

if(LOKA_WIN32_TOP_LEVEL_SOURCE_NORMALIZED STREQUAL
    LOKA_WIN32_REPOSITORY_ROOT_NORMALIZED)
  include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/LokaWin32VisualStyles.cmake)

  set(LOKA_WIN32_SCENARIO_PRESENTATION_COMMON_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/scenarios/ScenarioTypes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/scenarios/StartupScenarios.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/scenarios/RectSurfaceScenarioObservation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/TestingHooks.cpp
    ${SOURCES}
    ${COMMON_SOURCES}
  )

  set(LOKA_WIN32_SCENARIO_COMMON_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/win32/ScenarioDriverSupport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/scenarios/ScenarioProfile.cpp
    ${LOKA_WIN32_SCENARIO_PRESENTATION_COMMON_SOURCES}
  )

  function(loka_configure_win32_scenario_executable
      target example_include_dir output_directory)
    loka_enable_warnings(${target})
    loka_enable_win32_visual_styles(${target})
    target_include_directories(${target} PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/../common
      ${CMAKE_CURRENT_SOURCE_DIR}/../tests/win32
      ${CMAKE_CURRENT_SOURCE_DIR}/../tests/scenarios
      ${CMAKE_CURRENT_SOURCE_DIR}/../${example_include_dir}
      ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_compile_definitions(${target} PRIVATE TEST_BUILD)
    target_link_libraries(${target} PRIVATE
      LokaWin32WideApiContract
      windowscodecs
      ole32
    )
    set_target_properties(${target} PROPERTIES
      CXX_STANDARD 98
      CXX_STANDARD_REQUIRED YES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${output_directory}"
    )
    if(MSVC)
      target_compile_options(${target} PRIVATE /utf-8)
    endif()
  endfunction()

  function(loka_add_win32_scenario_executable
      target driver example_include_dir output_directory)
    # TEST-only vertical slices compile common and platform sources with one
    # uniform TEST_BUILD definition rather than linking the shipping core.
    add_executable(${target} WIN32 EXCLUDE_FROM_ALL
      ${CMAKE_CURRENT_SOURCE_DIR}/../tests/win32/${driver}
      ${LOKA_WIN32_SCENARIO_COMMON_SOURCES}
      ${ARGN}
    )
    loka_configure_win32_scenario_executable(
      ${target} ${example_include_dir} ${output_directory}
    )
  endfunction()

  loka_add_win32_scenario_executable(
    LokaScrapbookScenarioWin32
    ScrapbookScenarioMain.cpp
    example/ScrapbookUI/src
    example/ScrapbookUI
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/scenarios/ScrapbookScenarios.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../example/ScrapbookUI/src/ScrapbookPackage.cpp
  )
  loka_add_win32_scenario_executable(
    LokaHelloWorldScenarioWin32
    HelloWorldScenarioDriver.cpp
    example/HelloWorld/src
    example/HelloWorld
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/scenarios/HelloWorldScenarios.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../example/HelloWorld/src/MainNode.cpp
  )
  loka_add_win32_scenario_executable(
    LokaTutorialScenarioWin32
    TutorialScenarioDriver.cpp
    example/Tutorial/src
    example/Tutorial
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/scenarios/TutorialScenarios.cpp
  )
  loka_add_win32_scenario_executable(
    LokaMineSweeperScenarioWin32
    MineSweeperScenarioDriver.cpp
    example/MineSweeper/src
    example/MineSweeper
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/scenarios/MineSweeperScenarios.cpp
  )
  loka_add_win32_scenario_executable(
    LokaFloppyBirdScenarioWin32
    FloppyBirdScenarioDriver.cpp
    example/FloppyBird/src
    example/FloppyBird
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/scenarios/FloppyBirdScenarios.cpp
  )

  loka_add_win32_scenario_executable(
    LokaSmirkBenchScenarioWin32
    SmirkBenchScenarioDriver.cpp
    example/SmirkBench/src
    example/SmirkBench
  )

  # Self-contained loop presentations (#402, Win32 leg). These deliberately
  # omit ScenarioDriverSupport.cpp and ScenarioProfile.cpp: the double-clicked
  # reel has no host settings, audit publication, completion marker, or linger.
  function(loka_add_win32_scenario_loop_executable
      target main example_include_dir output_directory)
    add_executable(${target} WIN32 EXCLUDE_FROM_ALL
      ${CMAKE_CURRENT_SOURCE_DIR}/../tests/win32/${main}
      ${CMAKE_CURRENT_SOURCE_DIR}/../tests/scenarios/ScenarioReel.cpp
      ${LOKA_WIN32_SCENARIO_PRESENTATION_COMMON_SOURCES}
      ${ARGN}
    )
    loka_configure_win32_scenario_executable(
      ${target} ${example_include_dir} ${output_directory}
    )
    if(NOT LOKA_SCENARIO_LOOP_CYCLES STREQUAL "")
      target_compile_definitions(${target} PRIVATE
        LOKA_SCENARIO_LOOP_CYCLES=${LOKA_SCENARIO_LOOP_CYCLES}
      )
    endif()
  endfunction()

  set(LOKA_SCENARIO_LOOP_CYCLES "" CACHE STRING
    "Full reel passes a loop presentation runs before quitting. Empty means never quit."
  )
  add_test(NAME scriptWin32ScenarioProfile
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
      ${CMAKE_CURRENT_SOURCE_DIR}/../tests/scripts/Win32ScenarioProfileTest.ps1)

  add_test(NAME scriptWin32RigDescriptor
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
      ${CMAKE_CURRENT_SOURCE_DIR}/../tests/scripts/Win32RigDescriptorTest.ps1)

  loka_add_win32_scenario_loop_executable(
    LokaHelloWorldScenarioLoopWin32
    HelloWorldScenarioLoopMain.cpp
    example/HelloWorld/src
    example/HelloWorld/scenario-loop
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/scenarios/HelloWorldScenarios.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../example/HelloWorld/src/MainNode.cpp
  )
  loka_add_win32_scenario_loop_executable(
    LokaMineSweeperScenarioLoopWin32
    MineSweeperScenarioLoopMain.cpp
    example/MineSweeper/src
    example/MineSweeper/scenario-loop
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/scenarios/MineSweeperScenarios.cpp
  )

  if(LOKA_WIN32_SCENARIOS)
    set(_LOKA_WIN32_SCENARIO_REGISTRY
      "${CMAKE_CURRENT_SOURCE_DIR}/../tests/scenarios/scenarios.txt")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
      "${_LOKA_WIN32_SCENARIO_REGISTRY}")
    file(STRINGS "${_LOKA_WIN32_SCENARIO_REGISTRY}"
      _loka_win32_scenarios
      REGEX "^[a-z0-9][a-z0-9-]* [a-z0-9][a-z0-9-]*$")
    # The registry is shared with the Toolbox rail; only the examples the
    # Win32 runner has a vehicle for become tests here. Keep this list equal
    # to the $Vehicles table in tests/win32/run-scenario.ps1
    # (ScenarioToolsTest pins the two against each other).
    set(_LOKA_WIN32_SCENARIO_VEHICLES scrapbook helloworld tutorial minesweeper floppybird smirkbench)
    foreach(_loka_win32_entry IN LISTS _loka_win32_scenarios)
      string(REGEX REPLACE "^([^ ]+) ([^ ]+)$" "\\1"
        _loka_win32_example "${_loka_win32_entry}")
      string(REGEX REPLACE "^([^ ]+) ([^ ]+)$" "\\2"
        _loka_win32_scenario "${_loka_win32_entry}")
      if(NOT _loka_win32_example IN_LIST _LOKA_WIN32_SCENARIO_VEHICLES)
        continue()
      endif()
      # The other SmirkBench cells still assert Toolbox paint counters.
      if(_loka_win32_example STREQUAL "smirkbench"
          AND NOT _loka_win32_scenario MATCHES "^(startup|attributed-editor-line)$")
        continue()
      endif()
      add_test(NAME "win32Scenario-${_loka_win32_example}-${_loka_win32_scenario}"
        COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
          ${CMAKE_CURRENT_SOURCE_DIR}/../tests/win32/run-scenario.ps1
          "${_loka_win32_example}" "${_loka_win32_scenario}")
      set_tests_properties("win32Scenario-${_loka_win32_example}-${_loka_win32_scenario}"
        PROPERTIES LABELS "win32-scenario" RUN_SERIAL TRUE)
    endforeach()
    unset(_loka_win32_scenarios)
    unset(_LOKA_WIN32_SCENARIO_REGISTRY)
  endif()

  if(LOKA_ENABLE_STANDALONE_TARGETS)
  set(LOKA_STANDALONE_FLOW_ASSETS
    "${CMAKE_CURRENT_SOURCE_DIR}/../example/ScrapbookUI/assets/ASSETS-modern.LRP"
  )
  set(LOKA_STANDALONE_FLOW_OUTPUT_DIRECTORY
    "${CMAKE_BINARY_DIR}/standalone-flow"
  )
  set(LOKA_STANDALONE_LOOP_OUTPUT_DIRECTORY
    "${CMAKE_BINARY_DIR}/standalone-loop"
  )
  set(LOKA_WIN32_STANDALONE_FLOW_CATALOG
    "${CMAKE_BINARY_DIR}/standalone-flow-catalog.tsv"
  )
  file(WRITE "${LOKA_WIN32_STANDALONE_FLOW_CATALOG}"
    "# SimpleViewer is excluded from automation: its file chooser remains interactive.\n"
  )
  add_custom_target(LokaStandaloneFlowWin32All)
  add_custom_target(LokaStandaloneLoopWin32All)

  # TEST-only presentation target. Compile common/platform sources uniformly
  # with TEST_BUILD rather than mixing the shipping core's Node layout with the
  # TEST_ID-bearing scenario layout.
  set(LOKA_STANDALONE_PERFORMANCE_RUNS "" CACHE STRING
    "Complete Standalone Flow passes to measure (3-10). Empty disables measurement."
  )

  function(loka_add_win32_standalone_flow_executable
           key target loop_target config_header config_type example_include_dir expected_audit)
    # Finite targets own one complete App lifetime. Loop targets keep that App
    # and native Window, replacing the scenario rail and re-arming its Scene.
    foreach(_loka_standalone_target IN ITEMS ${target} ${loop_target})
      add_executable(${_loka_standalone_target} WIN32 EXCLUDE_FROM_ALL
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests/win32/StandaloneFlowMain.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests/standalone/StandaloneScenarioSupport.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests/scenarios/ScenarioReel.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests/scenarios/ScenarioTypes.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests/TestingHooks.cpp
        ${SOURCES}
        ${COMMON_SOURCES}
        ${ARGN}
      )
      loka_enable_warnings(${_loka_standalone_target})
      loka_enable_win32_visual_styles(${_loka_standalone_target})
      target_include_directories(${_loka_standalone_target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../common
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests/scenarios
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests/standalone
        ${CMAKE_CURRENT_SOURCE_DIR}/../${example_include_dir}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
      )
      target_compile_definitions(${_loka_standalone_target} PRIVATE
        TEST_BUILD
        "LOKA_STANDALONE_FLOW_CONFIG_HEADER=\"${config_header}\""
        "LOKA_STANDALONE_FLOW_CONFIG_TYPE=${config_type}"
      )
      target_link_libraries(${_loka_standalone_target} PRIVATE
        LokaWin32WideApiContract
        windowscodecs
        ole32
      )
      set_target_properties(${_loka_standalone_target} PROPERTIES
        CXX_STANDARD 98
        CXX_STANDARD_REQUIRED YES
        RUNTIME_OUTPUT_DIRECTORY "${LOKA_STANDALONE_FLOW_OUTPUT_DIRECTORY}/${key}"
      )
      if(MSVC)
        target_compile_options(${_loka_standalone_target} PRIVATE /utf-8)
      endif()
    endforeach()
    loka_enable_standalone_performance(
      ${target}
      ${CMAKE_CURRENT_SOURCE_DIR}/../tests/standalone/StandalonePerformance.cpp
    )
    target_compile_definitions(${loop_target} PRIVATE LOKA_STANDALONE_FLOW_LOOP=1)
    set_target_properties(${loop_target} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${LOKA_STANDALONE_LOOP_OUTPUT_DIRECTORY}/${key}"
    )
    add_dependencies(LokaStandaloneFlowWin32All ${target})
    add_dependencies(LokaStandaloneLoopWin32All ${loop_target})
    file(APPEND "${LOKA_WIN32_STANDALONE_FLOW_CATALOG}"
      "${key}\t${target}\t${expected_audit}\n"
    )
  endfunction()

  loka_add_win32_standalone_flow_executable(
    scrapbook
    LokaScrapbookStandaloneFlowWin32
    LokaScrapbookStandaloneLoopWin32
    ScrapbookStandaloneFlowAppConfig.hpp
    ::loka::standalone_tests::ScrapbookStandaloneFlowAppConfig
    example/ScrapbookUI/src
    tests/scenarios/expected/scrapbook/standalone-tour.audit
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/standalone/ScrapbookStandaloneFlowAppConfig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/scenarios/ScrapbookScenarios.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../example/ScrapbookUI/src/ScrapbookPackage.cpp
  )
  loka_add_win32_standalone_flow_executable(
    helloworld
    LokaHelloWorldStandaloneFlowWin32
    LokaHelloWorldStandaloneLoopWin32
    HelloWorldStandaloneFlowAppConfig.hpp
    ::loka::standalone_tests::HelloWorldStandaloneFlowAppConfig
    example/HelloWorld/src
    tests/scenarios/expected/helloworld/toggle-action-probe.audit
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/standalone/HelloWorldStandaloneFlowAppConfig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/scenarios/HelloWorldScenarios.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../example/HelloWorld/src/MainNode.cpp
  )
  loka_add_win32_standalone_flow_executable(
    tutorial
    LokaTutorialStandaloneFlowWin32
    LokaTutorialStandaloneLoopWin32
    TutorialStandaloneFlowAppConfig.hpp
    ::loka::standalone_tests::TutorialStandaloneFlowAppConfig
    example/Tutorial/src
    tests/scenarios/expected/tutorial/increment-summary-toggle.audit
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/standalone/TutorialStandaloneFlowAppConfig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/scenarios/TutorialScenarios.cpp
  )
  loka_add_win32_standalone_flow_executable(
    minesweeper
    LokaMineSweeperStandaloneFlowWin32
    LokaMineSweeperStandaloneLoopWin32
    MineSweeperStandaloneFlowAppConfig.hpp
    ::loka::standalone_tests::MineSweeperStandaloneFlowAppConfig
    example/MineSweeper/src
    tests/scenarios/expected/minesweeper/new-game-twice.audit
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/standalone/MineSweeperStandaloneFlowAppConfig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/scenarios/MineSweeperScenarios.cpp
  )
  loka_add_win32_standalone_flow_executable(
    floppybird
    LokaFloppyBirdStandaloneFlowWin32
    LokaFloppyBirdStandaloneLoopWin32
    FloppyBirdStandaloneFlowAppConfig.hpp
    ::loka::standalone_tests::FloppyBirdStandaloneFlowAppConfig
    example/FloppyBird/src
    tests/scenarios/expected/floppybird/fixed-step-flaps.audit
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/standalone/FloppyBirdStandaloneFlowAppConfig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/scenarios/FloppyBirdScenarios.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/scenarios/RectSurfaceScenarioObservation.cpp
  )

  # Read the owners' paths instead of restating the catalog-key directory.
  # TARGET_FILE_DIR would add reverse dependencies with the CMake 3.15 policy.
  get_target_property(LOKA_STANDALONE_FLOW_ASSET_OUTPUT_DIRECTORY
    LokaScrapbookStandaloneFlowWin32 RUNTIME_OUTPUT_DIRECTORY)
  get_target_property(LOKA_STANDALONE_LOOP_ASSET_OUTPUT_DIRECTORY
    LokaScrapbookStandaloneLoopWin32 RUNTIME_OUTPUT_DIRECTORY)
  if(CMAKE_CONFIGURATION_TYPES)
    string(APPEND LOKA_STANDALONE_FLOW_ASSET_OUTPUT_DIRECTORY "/$<CONFIG>")
    string(APPEND LOKA_STANDALONE_LOOP_ASSET_OUTPUT_DIRECTORY "/$<CONFIG>")
  endif()

  add_custom_target(LokaStandaloneWin32Assets
    COMMAND ${CMAKE_COMMAND} -E make_directory
      "${LOKA_STANDALONE_FLOW_ASSET_OUTPUT_DIRECTORY}"
      "${LOKA_STANDALONE_LOOP_ASSET_OUTPUT_DIRECTORY}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${LOKA_STANDALONE_FLOW_ASSETS}"
      "${LOKA_STANDALONE_FLOW_ASSET_OUTPUT_DIRECTORY}/ASSETS.LRP"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${LOKA_STANDALONE_FLOW_ASSETS}"
      "${LOKA_STANDALONE_LOOP_ASSET_OUTPUT_DIRECTORY}/ASSETS.LRP"
    DEPENDS "${LOKA_STANDALONE_FLOW_ASSETS}"
    VERBATIM
  )
  add_dependencies(LokaScrapbookStandaloneFlowWin32 LokaStandaloneWin32Assets)
  add_dependencies(LokaScrapbookStandaloneLoopWin32 LokaStandaloneWin32Assets)

  # Measurement-only vehicles: keep TEST_BUILD layouts uniform, as above.
  # Share Scrapbook's asset directory and the existing sidecar copy target.
  function(loka_add_win32_retirement_probe target main example_dir)
    add_executable(${target} WIN32 EXCLUDE_FROM_ALL
      ${CMAKE_CURRENT_SOURCE_DIR}/../tests/win32/${main}
      ${CMAKE_CURRENT_SOURCE_DIR}/../tests/win32/RetirementProbeSampler.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/../tests/TestingHooks.cpp
      ${SOURCES}
      ${COMMON_SOURCES}
      ${ARGN}
    )
    loka_enable_warnings(${target})
    loka_enable_win32_visual_styles(${target})
    target_include_directories(${target} PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/../tests/probes
      ${CMAKE_CURRENT_SOURCE_DIR}/../common
      ${CMAKE_CURRENT_SOURCE_DIR}/../tests/scenarios
      ${CMAKE_CURRENT_SOURCE_DIR}/../${example_dir}
      ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_compile_definitions(${target} PRIVATE TEST_BUILD)
    target_link_libraries(${target} PRIVATE LokaWin32WideApiContract windowscodecs ole32 psapi)
    set_target_properties(${target} PROPERTIES
      CXX_STANDARD 98
      CXX_STANDARD_REQUIRED YES
      RUNTIME_OUTPUT_DIRECTORY "${LOKA_STANDALONE_FLOW_OUTPUT_DIRECTORY}/scrapbook"
    )
    if(MSVC)
      target_compile_options(${target} PRIVATE /utf-8)
    endif()
  endfunction()

  loka_add_win32_retirement_probe(LokaRetirementProbeScrapbookWin32
    RetirementProbeScrapbookMain.cpp example/ScrapbookUI/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../example/ScrapbookUI/src/ScrapbookPackage.cpp)
  loka_add_win32_retirement_probe(LokaRetirementProbeDialogWin32
    RetirementProbeDialogMain.cpp example/SimpleViewer/src)
  loka_add_win32_retirement_probe(LokaRetirementProbeLazyWin32
    RetirementProbeLazyMain.cpp tests/win32)
  add_dependencies(LokaRetirementProbeScrapbookWin32 LokaStandaloneWin32Assets)
  add_custom_target(LokaRetirementProbeWin32All)
  add_dependencies(LokaRetirementProbeWin32All
    LokaRetirementProbeScrapbookWin32 LokaRetirementProbeDialogWin32 LokaRetirementProbeLazyWin32)

  unset(LOKA_STANDALONE_FLOW_OUTPUT_DIRECTORY)
  unset(LOKA_STANDALONE_LOOP_OUTPUT_DIRECTORY)
  unset(LOKA_STANDALONE_FLOW_ASSET_OUTPUT_DIRECTORY)
  unset(LOKA_STANDALONE_LOOP_ASSET_OUTPUT_DIRECTORY)
  unset(LOKA_STANDALONE_FLOW_ASSETS)
  unset(LOKA_WIN32_STANDALONE_FLOW_CATALOG)
  endif()
endif()

unset(LOKA_WIN32_TOP_LEVEL_SOURCE_NORMALIZED)
unset(LOKA_WIN32_REPOSITORY_ROOT_NORMALIZED)
unset(LOKA_WIN32_REPOSITORY_ROOT)
