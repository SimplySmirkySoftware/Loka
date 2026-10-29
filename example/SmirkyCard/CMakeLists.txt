cmake_minimum_required(VERSION 3.18)

include(cmake/QuickJS.cmake)

# Manifest readers in scripts/mame-*-disk*, scripts/release/classic-disks.py,
# scripts/win32-standalone-flow.ps1 and scripts/macos/standalone-release-stage.sh
# stay parallel with this reader.
set(SMIRKYCARD_DISK_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/disk-scripts.txt")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SMIRKYCARD_DISK_MANIFEST}")
file(STRINGS "${SMIRKYCARD_DISK_MANIFEST}" smirkycard_disk_lines)
set(SMIRKYCARD_DISK_SCRIPTS)
foreach(line IN LISTS smirkycard_disk_lines)
  string(STRIP "${line}" script)
  if(NOT script STREQUAL "" AND NOT script MATCHES "^#")
    list(APPEND SMIRKYCARD_DISK_SCRIPTS "${script}")
  endif()
endforeach()

# Classic LokaSimpleText opens 7-bit ASCII text only; see DiskScriptAscii.cmake.
include(cmake/DiskScriptAscii.cmake)
foreach(script IN LISTS SMIRKYCARD_DISK_SCRIPTS)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${script}")
  smirkycard_require_ascii_disk_script("${CMAKE_CURRENT_SOURCE_DIR}/${script}" "${script}")
endforeach()

include(cmake/DiskPictures.cmake)
smirkycard_read_disk_pictures("${CMAKE_CURRENT_SOURCE_DIR}" SMIRKYCARD_DISK_PICTURES)

set(SMIRKYCARD_BUILTIN_MAIN_JS ${CMAKE_CURRENT_SOURCE_DIR}/MAIN.JS)
set(SMIRKYCARD_BUILTIN_MAIN_HEADER ${CMAKE_CURRENT_BINARY_DIR}/BuiltinMainJs.inc)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${SMIRKYCARD_BUILTIN_MAIN_JS})
execute_process(
  COMMAND ${CMAKE_COMMAND}
    -DINPUT=${SMIRKYCARD_BUILTIN_MAIN_JS}
    -DOUTPUT=${SMIRKYCARD_BUILTIN_MAIN_HEADER}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedMainJs.cmake
  RESULT_VARIABLE smirkycard_embed_main_result
)
if(NOT smirkycard_embed_main_result EQUAL 0)
  message(FATAL_ERROR "Could not embed SmirkyCard MAIN.JS")
endif()

# A C-only seam keeps quickjs.h's modern macros out of Loka's C++98 code.
add_library(smirkycard_script STATIC src/ScriptEngine.c src/ScriptAlignedAlloc.c)
target_link_libraries(smirkycard_script PRIVATE smirkycard_quickjs)
target_include_directories(smirkycard_script PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(smirkycard_script PROPERTIES
  C_STANDARD ${SMIRKYCARD_C_STANDARD} C_STANDARD_REQUIRED YES)
if(MSVC)
  target_compile_options(smirkycard_script PRIVATE /W4 /wd4996 /utf-8)
  if(LOKA_WARNINGS_AS_ERRORS)
    target_compile_options(smirkycard_script PRIVATE /WX)
  endif()
else()
  target_compile_options(smirkycard_script PRIVATE -Wall -Wextra)
  if(LOKA_WARNINGS_AS_ERRORS)
    target_compile_options(smirkycard_script PRIVATE -Werror)
  endif()
endif()

if(LOKA_CLASSIC_MAC)
  target_compile_definitions(smirkycard_script PRIVATE LOKA_RETRO68=1)
  target_link_libraries(smirkycard_script PRIVATE LokaClassicNewlibCompat)
endif()

# Callers outside this directory (tests/toolbox/smirkycard) resolve the card
# sources and the generated headers against SmirkyCard's own directories.
set(SMIRKYCARD_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(SMIRKYCARD_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}")
function(smirkycard_cpp_target target)
  target_sources(${target} PRIVATE
    ${SMIRKYCARD_SOURCE_DIR}/src/CardFlow.cpp ${SMIRKYCARD_SOURCE_DIR}/src/CardScenario.cpp
    ${SMIRKYCARD_SOURCE_DIR}/src/CardCarry.cpp ${SMIRKYCARD_SOURCE_DIR}/src/ScriptRuntime.cpp
    ${SMIRKYCARD_SOURCE_DIR}/src/JsCardNode.cpp ${SMIRKYCARD_SOURCE_DIR}/src/JsCardBindingRegistry.cpp
    ${SMIRKYCARD_SOURCE_DIR}/src/SmirkyMarkup.cpp)
  loka_enable_warnings(${target})
  set_target_properties(${target} PROPERTIES CXX_STANDARD 98 CXX_STANDARD_REQUIRED YES)
  target_link_libraries(${target} PRIVATE smirkycard_script)
  # JsCardNode is the deliberately small C++ owner of values crossing the
  # QuickJS boundary.  The engine remains a C target; its public header is
  # safe to include from this C++98 boundary.
  target_link_libraries(${target} PRIVATE smirkycard_quickjs)
  target_include_directories(${target} SYSTEM PRIVATE ${smirkycard_quickjs_source_dir})
  target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/common)
  target_include_directories(${target} PRIVATE ${SMIRKYCARD_BINARY_DIR})
  if(MSVC)
    target_compile_options(${target} PRIVATE /utf-8)
  endif()
endfunction()

if(WIN32)
  include(${PROJECT_SOURCE_DIR}/cmake/LokaWin32VisualStyles.cmake)
  add_executable(LokaSmirkyCardWin32 WIN32 src/main.cpp)
  smirkycard_cpp_target(LokaSmirkyCardWin32)
  loka_enable_win32_visual_styles(LokaSmirkyCardWin32)
  target_link_libraries(LokaSmirkyCardWin32 PRIVATE LokaWin32Core)
  foreach(script IN LISTS SMIRKYCARD_DISK_SCRIPTS)
    configure_file("${script}" "${script}" COPYONLY)
  endforeach()
  foreach(stem IN LISTS SMIRKYCARD_DISK_PICTURES)
    configure_file("assets/${stem}.PNG" "Pictures/${stem}.PNG" COPYONLY)
  endforeach()
elseif(LOKA_MACOS AND LOKA_ENABLE_APPLE)
  enable_language(OBJCXX)
  loka_suppress_arclite_link()
  set(SMIRKYCARD_MACOS_RESOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/macos-resources)
  file(MAKE_DIRECTORY ${SMIRKYCARD_MACOS_RESOURCE_DIR})
  set(SMIRKYCARD_MACOS_RESOURCES)
  foreach(script IN LISTS SMIRKYCARD_DISK_SCRIPTS)
    set(resource "${SMIRKYCARD_MACOS_RESOURCE_DIR}/${script}")
    configure_file("${script}" "${resource}" COPYONLY)
    list(APPEND SMIRKYCARD_MACOS_RESOURCES "${resource}")
  endforeach()
  set(SMIRKYCARD_MACOS_PICTURES)
  foreach(stem IN LISTS SMIRKYCARD_DISK_PICTURES)
    set(resource "${SMIRKYCARD_MACOS_RESOURCE_DIR}/${stem}.PNG")
    configure_file("assets/${stem}.PNG" "${resource}" COPYONLY)
    list(APPEND SMIRKYCARD_MACOS_PICTURES "${resource}")
    list(APPEND SMIRKYCARD_MACOS_RESOURCES "${resource}")
  endforeach()
  set_source_files_properties(${SMIRKYCARD_MACOS_RESOURCES} PROPERTIES
    GENERATED TRUE
    MACOSX_PACKAGE_LOCATION Resources
  )
  set_source_files_properties(${SMIRKYCARD_MACOS_PICTURES} PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources/Pictures
  )
  add_executable(LokaSmirkyCardMacOS MACOSX_BUNDLE src/MacOSMain.mm
    ${SMIRKYCARD_MACOS_RESOURCES})
  smirkycard_cpp_target(LokaSmirkyCardMacOS)
  # The open panel's view service keys its defaults by the bundle identifier and
  # refuses to display without one (NSUserDefaults: domain name cannot be nil).
  set_target_properties(LokaSmirkyCardMacOS PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.loka.smirkycard"
    MACOSX_BUNDLE_BUNDLE_NAME "LokaSmirkyCardMacOS"
  )
  target_link_libraries(LokaSmirkyCardMacOS PRIVATE LokaMacOSCore)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND LOKA_MACOS_EXPLICIT_NO_ARC_FLAGS)
    target_compile_options(LokaSmirkyCardMacOS PRIVATE -fno-objc-arc)
  endif()
endif()

# PowerPC loads its PEF code into the heap; SizePPC.r budgets that separately.
if(CMAKE_SYSTEM_NAME STREQUAL "RetroPPC")
  set(SMIRKYCARD_SIZE_R SizePPC.r)
else()
  set(SMIRKYCARD_SIZE_R Size.r)
endif()

if(CMAKE_SYSTEM_NAME MATCHES "^Retro(68|PPC|Carbon)$")
  set(LOKA_SMIRKYCARD_CLASSIC_TARGET "LokaSmirkyCard${LOKA_CLASSIC_TARGET_SUFFIX}")
  add_application(${LOKA_SMIRKYCARD_CLASSIC_TARGET}
    src/ToolboxMain.cpp
    ${SMIRKYCARD_SIZE_R}
  )
  smirkycard_cpp_target(${LOKA_SMIRKYCARD_CLASSIC_TARGET})
  target_link_libraries(${LOKA_SMIRKYCARD_CLASSIC_TARGET} PRIVATE LokaAppleToolboxCore)
  target_compile_definitions(${LOKA_SMIRKYCARD_CLASSIC_TARGET} PRIVATE LOKA_RETRO68=1)
  target_include_directories(${LOKA_SMIRKYCARD_CLASSIC_TARGET} PRIVATE ${PROJECT_SOURCE_DIR}/apple/toolbox/src)
  target_compile_options(${LOKA_SMIRKYCARD_CLASSIC_TARGET} PRIVATE
    -Os
    -ffunction-sections
    -fdata-sections
    -fno-exceptions
    -fno-rtti
  )
  target_link_options(${LOKA_SMIRKYCARD_CLASSIC_TARGET} PRIVATE -Wl,--gc-sections)
  if(CMAKE_SYSTEM_NAME STREQUAL "Retro68" AND NOT LOKA_RETRO68_DIAGNOSTICS)
    target_link_options(${LOKA_SMIRKYCARD_CLASSIC_TARGET} PRIVATE -Wl,--mac-strip-macsbug)
  endif()
endif()

if(TARGET LokaCommonTesting)
  set(smirkycard_null_sources ${LOKA_SHARED_TEST_SOURCES})
  list(FILTER smirkycard_null_sources INCLUDE REGEX "/tests/platform/null/.*\\.cpp$")
  add_executable(LokaSmirkyCardTests
    tests/SmirkyCardTests.cpp
    ${PROJECT_SOURCE_DIR}/tests/TestingHooks.cpp
    ${smirkycard_null_sources}
  )
  smirkycard_cpp_target(LokaSmirkyCardTests)
  target_link_libraries(LokaSmirkyCardTests PRIVATE LokaCommonTesting)
  target_compile_definitions(LokaSmirkyCardTests PRIVATE
    SMIRKYCARD_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME smirkyCardSceneSwitch COMMAND LokaSmirkyCardTests)
  add_test(NAME smirkyCardMainResult COMMAND LokaSmirkyCardTests --main-result)
endif()

# One call plus two JS files adds a scenario; the config and main contain no card names.
function(smirkycard_add_scenario_inputs name)
  cmake_parse_arguments(SCENARIO "" "CARD;FLOW;SCENARIO;APP_NAME;SEED" "" ${ARGN})
  set(embed_dir "${CMAKE_CURRENT_BINARY_DIR}/${name}-scenario")
  file(MAKE_DIRECTORY "${embed_dir}")
  foreach(kind IN ITEMS Card Flow)
    if(kind STREQUAL "Card")
      set(input "${SCENARIO_CARD}")
    else()
      set(input "${SCENARIO_FLOW}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${input}")
    execute_process(COMMAND ${CMAKE_COMMAND}
      "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/${input}"
      "-DOUTPUT=${embed_dir}/Scenario${kind}.inc"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedMainJs.cmake"
      RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
      message(FATAL_ERROR "Could not embed ${input}")
    endif()
  endforeach()
  add_library(${name}ScenarioInputs INTERFACE)
  target_include_directories(${name}ScenarioInputs INTERFACE "${embed_dir}")
  target_compile_definitions(${name}ScenarioInputs INTERFACE
    SMIRKYCARD_SCENARIO_SEED=${SCENARIO_SEED}
    "SMIRKYCARD_SCENARIO_NAME=\"${SCENARIO_SCENARIO}\""
    "SMIRKYCARD_SCENARIO_APP_NAME=\"${SCENARIO_APP_NAME}\""
    "SMIRKYCARD_SCENARIO_COMPANION=\"${SCENARIO_FLOW}\"")
endfunction()

function(smirkycard_add_scenario_runner name)
  cmake_parse_arguments(SCENARIO "" "CARD;FLOW;SCENARIO;APP_NAME;SEED" "" ${ARGN})
  smirkycard_add_scenario_inputs(${name} ${ARGN})
  # Registered whenever SmirkyCard builds for Classic, so the SmirkyCard build
  # task makes the app and its scenario runner together (EXCLUDE_FROM_ALL).
  if(CMAKE_SYSTEM_NAME MATCHES "^Retro(68|PPC)$")
    set(target "Loka${name}StandaloneFlow${LOKA_CLASSIC_TARGET_SUFFIX}")
    # The APPL keeps the target name on the HFS dev disk, whose names stop at 31 characters.
    string(LENGTH "${target}" target_length)
    if(target_length GREATER 31)
      message(FATAL_ERROR "${target} is ${target_length} characters; HFS file names allow 31. Shorten the runner name '${name}'.")
    endif()
    loka_add_toolbox_test_application(${target} LokaAppleToolboxTestCore
      src/StandaloneFlowMain.cpp src/StandaloneFlowAppConfig.cpp src/ScenarioRunAudit.cpp ${SMIRKYCARD_SIZE_R}
      ${PROJECT_SOURCE_DIR}/tests/standalone/StandaloneScenarioSupport.cpp
      ${PROJECT_SOURCE_DIR}/tests/scenarios/ScenarioReel.cpp
      ${PROJECT_SOURCE_DIR}/tests/scenarios/ScenarioTypes.cpp
      ${PROJECT_SOURCE_DIR}/tests/TestingHooks.cpp)
    smirkycard_cpp_target(${target})
    target_link_libraries(${target} PRIVATE ${name}ScenarioInputs)
    loka_configure_toolbox_standalone_target(${target}
      "${CMAKE_CURRENT_SOURCE_DIR}/src" "LokaStandaloneFlow${LOKA_CLASSIC_TARGET_SUFFIX}All")
  endif()
endfunction()

smirkycard_add_scenario_runner(SmirkyCard CARD MINES.JS FLOW MINES.FLOW.JS
  SCENARIO mines APP_NAME "SmirkyCard Mines" SEED 7)
smirkycard_add_scenario_runner(SmirkyView CARD VIEWER.JS FLOW VIEWER.FLOW.JS SCENARIO viewer APP_NAME "SmirkyCard Viewer" SEED 7)
smirkycard_add_scenario_runner(Laptop CARD LAPTOP.JS FLOW LAPTOP.FLOW.JS SCENARIO laptop APP_NAME "SmirkyTracker" SEED 7)
if(TARGET LokaSmirkyCardTests)
  target_link_libraries(LokaSmirkyCardTests PRIVATE SmirkyCardScenarioInputs)
  target_sources(LokaSmirkyCardTests PRIVATE src/StandaloneFlowAppConfig.cpp src/ScenarioRunAudit.cpp
    ${PROJECT_SOURCE_DIR}/tests/standalone/StandaloneScenarioSupport.cpp
    ${PROJECT_SOURCE_DIR}/tests/scenarios/ScenarioReel.cpp
    ${PROJECT_SOURCE_DIR}/tests/scenarios/ScenarioTypes.cpp)
  target_include_directories(LokaSmirkyCardTests PRIVATE ${PROJECT_SOURCE_DIR}/tests/standalone)
endif()

# The Toolbox scenario vehicle is a tests/toolbox resident that needs QuickJS and
# the SmirkyCard SIZE partition, so it is declared from here but builds into
# tests/toolbox/smirkycard: the 68K size bank discovers only example/ artifacts.
if(CMAKE_SYSTEM_NAME STREQUAL "Retro68")
  add_subdirectory(${PROJECT_SOURCE_DIR}/tests/toolbox/smirkycard ${PROJECT_BINARY_DIR}/tests/toolbox/smirkycard)
endif()

# MAIN.JS measurement companion and Classic vehicle (Toolbar 1a-0).
smirkycard_add_scenario_inputs(MainResult CARD MAIN.JS FLOW MAIN.FLOW.JS
  SCENARIO main-result APP_NAME "SmirkyCard" SEED 7)
if(CMAKE_SYSTEM_NAME MATCHES "^Retro(68|PPC)$")
  set(target "LokaMeasureMain${LOKA_CLASSIC_TARGET_SUFFIX}")
  loka_add_toolbox_test_application(${target} LokaAppleToolboxTestCore
    ${PROJECT_SOURCE_DIR}/tests/toolbox/src/ToolbarMeasurementMain.cpp
    src/ScenarioRunAudit.cpp ${SMIRKYCARD_SIZE_R}
    ${PROJECT_SOURCE_DIR}/tests/scenarios/ScenarioTypes.cpp
    ${PROJECT_SOURCE_DIR}/tests/TestingHooks.cpp)
  smirkycard_cpp_target(${target})
  target_link_libraries(${target} PRIVATE MainResultScenarioInputs)
  target_compile_definitions(${target} PRIVATE TEST_BUILD LOKA_DIAG=1 TOOLBAR_APP=4)
endif()
