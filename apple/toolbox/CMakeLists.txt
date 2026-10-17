cmake_minimum_required(VERSION 3.15)

# Toolbox builds are expected to use a separate toolchain (e.g. Retro68).
add_library(LokaClassicNewlibCompat INTERFACE)
target_include_directories(LokaClassicNewlibCompat BEFORE INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/compat/newlib
)

add_library(LokaAppleToolbox INTERFACE)

set(LOKA_TOOLBOX_SOURCES
  src/ToolboxCpuClass.cpp
  src/ToolboxBootstrap.cpp
  src/ToolboxOperatorNew.cpp
  src/ToolboxOutOfMemory.cpp
  src/ToolboxDialogConstruction.cpp
  src/ToolboxReserveGrowZone.cpp
  src/ToolboxString.cpp
  src/ToolboxApp.cpp
  src/ToolboxMenuAttachment.cpp
  src/ToolboxByteSource.cpp
  src/ToolboxNativeImage.cpp
  src/PictParser.cpp
  src/ToolboxProfiler.cpp
  src/ToolboxBuiltInSupport.cpp
  src/debug/ToolboxSceneDebugStats.cpp
  src/context/ToolboxButtonContext.cpp
  src/context/ToolboxLayoutUtil.cpp
  src/context/ToolboxProjectedNodeContext.cpp
  src/context/ToolboxCellContext.cpp
  src/context/ToolboxRibbonGroupEdgeContext.cpp
  src/context/ToolboxCardBandContext.cpp
  src/context/ToolboxTextEditorContext.cpp
  src/context/ToolboxEditTextContext.cpp
  src/ToolboxPendingDialogs.cpp
  src/context/ToolboxConfirmDialogContext.cpp
  src/context/ToolboxOpenFileDialogContext.cpp
  src/context/ToolboxPopupMenuContext.cpp
  src/context/ToolboxScrollBarContext.cpp
  src/context/ToolboxAttributedTextContext.cpp
  src/context/ToolboxAttributedTextTable.cpp
  src/context/ToolboxTextContext.cpp
  src/context/ToolboxImageViewContext.cpp
  src/context/ToolboxRectSurfaceContext.cpp
  src/ToolboxPlatformLayoutHandlers.cpp
  src/ToolboxTextEditorBinding.cpp
  src/ToolboxEditTextBinding.cpp
  src/ToolboxNativeRetirement.cpp
  src/ToolboxHitLedger.cpp
  src/ToolboxNodeDispatch.cpp
  src/ToolboxScrollBarLedger.cpp
  src/ToolboxScenePlatformController.cpp
  src/ToolboxNaturalWidth.cpp
  src/ToolboxWindow.cpp
  src/ToolboxWindowContext.cpp
  src/ToolboxFileChoice.cpp
  src/ToolboxPlatformContext.cpp
  src/ToolboxBusy.cpp
  src/platform/ToolboxAppLocation.cpp
  src/platform/ToolboxFileIO.cpp
  src/platform/ToolboxHfsName.cpp
  src/platform/ToolboxMacRoman.cpp
  src/platform/ToolboxPascalText.cpp
)

# These implementation cells are included by the controller so the Classic
# compiler retains the original translation-unit optimization and code size.
set_source_files_properties(
  src/ToolboxTextEditorBinding.cpp
  src/ToolboxEditTextBinding.cpp
  src/ToolboxNativeRetirement.cpp
  src/ToolboxHitLedger.cpp
  src/ToolboxScrollBarLedger.cpp
  PROPERTIES HEADER_FILE_ONLY TRUE
)

file(GLOB_RECURSE COMMON_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/common/*.cpp)

function(loka_configure_toolbox_core target)
  loka_enable_warnings(${target})
  set_target_properties(${target} PROPERTIES
    CXX_STANDARD 98
    CXX_STANDARD_REQUIRED YES
  )

  if(CMAKE_SYSTEM_NAME MATCHES "^Retro(68|PPC|Carbon)$")
    target_compile_options(${target} PRIVATE
      -Os
      -ffunction-sections
      -fdata-sections
      -fno-exceptions
      -fno-rtti
    )
    if(LOKA_TOOLBOX_CLASSIC_6)
      target_compile_definitions(${target} PRIVATE LOKA_TOOLBOX_CLASSIC_6=1)
    endif()
    # LOKA_PROFILE_FUNC_TICKS / LOKA_RETRO68_DIAGNOSTICS come from the
    # top-level LOKA_RETRO68_DIAGNOSTICS option (#135).
    target_compile_definitions(${target} PRIVATE LOKA_RETRO68=1)
    target_link_options(${target} PRIVATE
      -Wl,--gc-sections
    )
  endif()

  target_link_libraries(${target} PUBLIC
    LokaAppleToolbox
  )

  target_include_directories(${target} PUBLIC
    ${PROJECT_SOURCE_DIR}/common
  )

  target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
  )
endfunction()

add_library(LokaAppleToolboxCore STATIC ${LOKA_TOOLBOX_SOURCES} ${COMMON_SOURCES})
loka_configure_toolbox_core(LokaAppleToolboxCore)

if(CMAKE_SYSTEM_NAME MATCHES "^Retro(68|PPC)$")
  # Standalone scenario applications need TEST_ID-bearing inline definitions
  # to have one meaning throughout the executable. Keep this variant explicit
  # and out of ordinary builds rather than mixing it with the shipping core.
  add_library(LokaAppleToolboxTestCore STATIC EXCLUDE_FROM_ALL
    ${LOKA_TOOLBOX_SOURCES}
    ${COMMON_SOURCES}
  )
  target_compile_definitions(LokaAppleToolboxTestCore PRIVATE
    TEST_BUILD
    LOKA_DIAG=1
  )
  loka_configure_toolbox_core(LokaAppleToolboxTestCore)
endif()

target_link_libraries(LokaAppleToolbox INTERFACE
  LokaClassicNewlibCompat
  LokaAppleCommonCarbon
)

target_include_directories(LokaAppleToolbox INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}
)

if(LOKA_TOOLBOX_MULTIVERSAL_INTERFACES)
  target_include_directories(LokaAppleToolbox BEFORE INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/compat/multiversal
  )
  target_compile_definitions(LokaAppleToolbox INTERFACE
    LOKA_TOOLBOX_MULTIVERSAL_INTERFACES=1
  )
endif()
