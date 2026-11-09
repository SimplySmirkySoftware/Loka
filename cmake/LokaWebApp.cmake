# Definitions only: safe to include on native platforms.
get_filename_component(LOKA_WEB_RAIL_DIR "${CMAKE_CURRENT_LIST_DIR}/../web" ABSOLUTE)

function(loka_add_web_app TARGET EXPORT_NAME)
  add_executable(${TARGET} ${ARGN} ${LOKA_WEB_RAIL_DIR}/entry/WebEntry.cpp)
  target_include_directories(${TARGET} PRIVATE ${LOKA_WEB_RAIL_DIR}/entry)
  target_link_libraries(${TARGET} PRIVATE LokaWebCore)
  target_compile_options(${TARGET} PRIVATE -UTEST_BUILD -ULOKA_DIAG -fno-exceptions -fno-rtti -Werror)
  set_target_properties(${TARGET} PROPERTIES
    CXX_STANDARD 98 CXX_STANDARD_REQUIRED YES OUTPUT_NAME ${EXPORT_NAME})
  loka_enable_warnings(${TARGET})
  target_link_options(${TARGET} PRIVATE
    -sSTACK_SIZE=8MB -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=0
    -sMODULARIZE=1 -sEXPORT_NAME=${EXPORT_NAME} -sENVIRONMENT=web
    -sDYNAMIC_EXECUTION=0 -sEXPORT_ES6=0 -sINVOKE_RUN=1 -sWASM_ASYNC_COMPILATION=1
    "-sINCOMING_MODULE_JS_API=['locateFile']"
    "--js-library=${LOKA_WEB_RAIL_DIR}/js/library.js"
    "--pre-js=${LOKA_WEB_RAIL_DIR}/js/style-vocab.js"
    "--pre-js=${LOKA_WEB_RAIL_DIR}/js/text.js"
    "--post-js=${LOKA_WEB_RAIL_DIR}/js/api.js")
  set_property(TARGET ${TARGET} APPEND PROPERTY LINK_DEPENDS
    ${LOKA_WEB_RAIL_DIR}/js/library.js
    ${LOKA_WEB_RAIL_DIR}/js/style-vocab.js
    ${LOKA_WEB_RAIL_DIR}/js/text.js
    ${LOKA_WEB_RAIL_DIR}/js/api.js)
endfunction()
