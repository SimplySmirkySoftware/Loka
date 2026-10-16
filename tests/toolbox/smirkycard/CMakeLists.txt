# LokaSmirkyCardTest68K: the run-scenario.sh vehicle for the smirkycard cells.
# Declared from example/SmirkyCard/CMakeLists.txt (it links the QuickJS card
# runtime and the SmirkyCard SIZE partition) and built here so the artifact
# lives beside the other scenario vehicles instead of in example/.
loka_add_toolbox_test_application(LokaSmirkyCardTest68K LokaAppleToolboxTestCore
  ${PROJECT_SOURCE_DIR}/tests/toolbox/src/SmirkyCardScenarioMain.cpp
  ${PROJECT_SOURCE_DIR}/tests/toolbox/src/SmirkyCardScenarioDriver.cpp
  ${PROJECT_SOURCE_DIR}/tests/toolbox/src/ScenarioDriverSupport.cpp
  ${PROJECT_SOURCE_DIR}/example/SmirkyCard/src/ScenarioRunAudit.cpp
  ${PROJECT_SOURCE_DIR}/example/SmirkyCard/${SMIRKYCARD_SIZE_R}
  ${PROJECT_SOURCE_DIR}/tests/scenarios/ScenarioTypes.cpp
  ${PROJECT_SOURCE_DIR}/tests/TestingHooks.cpp)
# run-scenario.sh reads classic-build-provenance.txt beside the vehicle.
add_custom_target(LokaSmirkyCardScenarioProvenance
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${PROJECT_BINARY_DIR}/tests/toolbox/classic-build-provenance.txt"
    "${CMAKE_CURRENT_BINARY_DIR}/classic-build-provenance.txt"
  DEPENDS LokaToolboxBuildProvenance
  VERBATIM)
add_dependencies(LokaSmirkyCardTest68K LokaSmirkyCardScenarioProvenance)
smirkycard_cpp_target(LokaSmirkyCardTest68K)
target_link_libraries(LokaSmirkyCardTest68K PRIVATE LaptopScenarioInputs)
target_include_directories(LokaSmirkyCardTest68K PRIVATE ${PROJECT_SOURCE_DIR}/tests/toolbox/src)
target_compile_definitions(LokaSmirkyCardTest68K PRIVATE TEST_BUILD LOKA_DIAG=1)
