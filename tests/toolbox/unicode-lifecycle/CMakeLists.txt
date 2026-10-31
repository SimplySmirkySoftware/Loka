add_application(LokaToolboxUnicodeLifecycle main.cpp Size.r)
loka_toolbox_unicode(LokaToolboxUnicodeLifecycle)
target_include_directories(LokaToolboxUnicodeLifecycle PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../apple/toolbox/src)
loka_enable_warnings(LokaToolboxUnicodeLifecycle)
set_target_properties(LokaToolboxUnicodeLifecycle PROPERTIES
  CXX_STANDARD 98 CXX_STANDARD_REQUIRED YES)
