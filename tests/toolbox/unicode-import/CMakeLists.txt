# Link-only acceptance: these references intentionally exercise all three imports.
add_application(LokaToolboxUnicodeImportProbe main.cpp)
loka_toolbox_unicode(LokaToolboxUnicodeImportProbe)
target_include_directories(LokaToolboxUnicodeImportProbe PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../apple/toolbox/src
)
loka_enable_warnings(LokaToolboxUnicodeImportProbe)
set_target_properties(LokaToolboxUnicodeImportProbe PROPERTIES
  CXX_STANDARD 98 CXX_STANDARD_REQUIRED YES)
