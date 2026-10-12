# Fetch only the engine sources; the upstream project also builds CLI tools.
include(FetchContent)
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()
FetchContent_Declare(smirkycard_quickjs
  URL https://codeload.github.com/quickjs-ng/quickjs/tar.gz/1ab8676f4b6d6d669baeb5f21790fb9734636a20
  URL_HASH SHA256=c788fe4f65c95ecfa4055c8778e7cb221f68fcc3315686627b0856da5c38514e
  SOURCE_SUBDIR loka-embedded-engine-only
)
FetchContent_MakeAvailable(smirkycard_quickjs)

set(smirkycard_quickjs_source_dir "${smirkycard_quickjs_SOURCE_DIR}")
if(LOKA_CLASSIC_MAC)
  find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/prepare_quickjs68k.py")
  set(smirkycard_quickjs_classic_source
    "${CMAKE_CURRENT_BINARY_DIR}/smirkycard-quickjs-classic-source")
  execute_process(
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_LIST_DIR}/prepare_quickjs68k.py"
      "${smirkycard_quickjs_SOURCE_DIR}"
      "${smirkycard_quickjs_classic_source}"
    RESULT_VARIABLE smirkycard_quickjs_prepare_result
  )
  if(NOT smirkycard_quickjs_prepare_result EQUAL 0)
    message(FATAL_ERROR "Classic QuickJS source preparation failed")
  endif()
  set(smirkycard_quickjs_source_dir "${smirkycard_quickjs_classic_source}")
endif()

# QuickJS-ng is C11. The legacy release toolchains have no C11 (Visual Studio
# 2017 for the XP profile, GCC 4.0 for Tiger UB1): they build it as C99 with
# QuickJSLegacyC.h supplying the one C11 keyword QuickJS uses. Ask the compiler
# itself: CMake lists c_std_11 for MSVC 19.16, which has no _Alignas. Every
# other compiler keeps C11; smirkycard_script follows the same dialect.
include(CheckCSourceCompiles)
check_c_source_compiles(
  "struct smirkycard_probe { _Alignas(8) unsigned char bytes[8]; }; int main(void) { return 0; }"
  SMIRKYCARD_C_HAS_ALIGNAS)
if(SMIRKYCARD_C_HAS_ALIGNAS)
  set(SMIRKYCARD_C_STANDARD 11)
else()
  set(SMIRKYCARD_C_STANDARD 99)
endif()

# QuickJS-ng v0.16.2. Keep the dependency's C dialect and platform flags local.
add_library(smirkycard_quickjs STATIC
  ${smirkycard_quickjs_source_dir}/quickjs.c
  ${smirkycard_quickjs_source_dir}/dtoa.c
  ${smirkycard_quickjs_source_dir}/libregexp.c
  ${smirkycard_quickjs_source_dir}/libunicode.c
)
set_target_properties(smirkycard_quickjs PROPERTIES
  C_STANDARD ${SMIRKYCARD_C_STANDARD} C_STANDARD_REQUIRED YES)
if(SMIRKYCARD_C_STANDARD EQUAL 99)
  if(MSVC)
    target_compile_options(smirkycard_quickjs PRIVATE
      "/FI${CMAKE_CURRENT_LIST_DIR}/QuickJSLegacyC.h")
  else()
    # SHELL: keeps -include paired with its file; CMake de-duplicates bare
    # options, which would orphan the second header's path.
    target_compile_options(smirkycard_quickjs PRIVATE
      "SHELL:-include \"${CMAKE_CURRENT_LIST_DIR}/QuickJSLegacyC.h\"")
  endif()
endif()
# A separate question from the dialect: does the configured Apple SDK declare
# the monotonic clock QuickJS reads? The 10.4u SDK does not, with GCC 4.0 or
# with clang; SDKs from 10.12 do, and keep their own clock_gettime.
if(APPLE)
  include(CheckSymbolExists)
  check_symbol_exists(CLOCK_MONOTONIC "time.h" SMIRKYCARD_SDK_HAS_CLOCK_MONOTONIC)
  if(NOT SMIRKYCARD_SDK_HAS_CLOCK_MONOTONIC)
    target_compile_options(smirkycard_quickjs PRIVATE
      "SHELL:-include \"${CMAKE_CURRENT_LIST_DIR}/QuickJSMachClock.h\"")
  endif()
endif()
target_include_directories(smirkycard_quickjs SYSTEM PUBLIC ${smirkycard_quickjs_source_dir})
# The experiment executes synchronously on the main thread; no Atomics/workers.
target_compile_definitions(smirkycard_quickjs PRIVATE _GNU_SOURCE __STDC_NO_ATOMICS__=1)
if(MSVC)
  target_compile_options(smirkycard_quickjs PRIVATE /utf-8)
  target_compile_definitions(smirkycard_quickjs PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_link_libraries(smirkycard_quickjs PRIVATE m)
endif()
if(LOKA_CLASSIC_MAC OR LOKA_MAC_OS_10_4)
  # quickjs.h uses INT32_MIN/MAX in C++98 inline functions. GCC's stdint.h
  # (Retro68, and GCC 4.0 for Tiger UB1) requires this opt-in, which must reach
  # every consumer before any include.
  target_compile_definitions(smirkycard_quickjs PUBLIC __STDC_LIMIT_MACROS)
endif()
if(LOKA_CLASSIC_MAC)
  target_compile_definitions(smirkycard_quickjs PRIVATE LOKA_SMIRKYCARD_QUICKJS_68K=1)
  target_include_directories(smirkycard_quickjs PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../src")
  if(LOKA_TOOLBOX_MULTIVERSAL_INTERFACES)
    target_compile_definitions(smirkycard_quickjs PRIVATE LOKA_TOOLBOX_MULTIVERSAL_INTERFACES=1)
  endif()
  target_link_libraries(smirkycard_quickjs PRIVATE LokaClassicNewlibCompat)
  target_compile_options(smirkycard_quickjs PRIVATE -Os -ffunction-sections -fdata-sections)
  target_sources(smirkycard_quickjs PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../src/ToolboxClock.c")
endif()
