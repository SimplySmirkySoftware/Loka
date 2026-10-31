# The MLTE declaration subset must compile alone against whichever interface
# set the build uses; this object is that pin and is part of every PPC build.
# The SDK comparison (compare_subset.py) is a local tool and is not built here.
add_library(LokaToolboxMlteSubsetStandalone OBJECT subset_standalone.cpp)
target_include_directories(LokaToolboxMlteSubsetStandalone PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../apple/toolbox/src
)
target_link_libraries(LokaToolboxMlteSubsetStandalone PRIVATE LokaAppleToolbox)
loka_enable_warnings(LokaToolboxMlteSubsetStandalone)
set_target_properties(LokaToolboxMlteSubsetStandalone PROPERTIES
  CXX_STANDARD 98 CXX_STANDARD_REQUIRED YES)
