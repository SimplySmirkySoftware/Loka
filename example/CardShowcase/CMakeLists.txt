cmake_minimum_required(VERSION 3.15)
project(LokaCardShowcase LANGUAGES CXX)

if(APPLE)
  enable_language(OBJCXX)
  loka_suppress_arclite_link()
endif()

if(NOT APPLE)
  add_custom_target(LokaCardShowcase ALL
    COMMAND ${CMAKE_COMMAND} -E echo "LokaCardShowcase has no supported platform target here."
  )
  return()
endif()

set(CARDSHOWCASE_PHOTOS
  ${CMAKE_CURRENT_SOURCE_DIR}/../../assets/LokaLaptops.jpg
  ${CMAKE_CURRENT_SOURCE_DIR}/../../goodies/LokaFlyer_0.0.1_PBG4.jpg
)

if(APPLE)
  set(MACOS_SOURCES
    src/MacOSMain.mm
  )
  file(GLOB_RECURSE MACOS_HEADERS src/*.hpp)

  set(CARDSHOWCASE_MACOS_RESOURCES)
  foreach(photo IN LISTS CARDSHOWCASE_PHOTOS)
    get_filename_component(photo_name "${photo}" NAME)
    set(resource "${CMAKE_CURRENT_BINARY_DIR}/macos-resources/${photo_name}")
    configure_file("${photo}" "${resource}" COPYONLY)
    set_source_files_properties("${resource}" PROPERTIES
      MACOSX_PACKAGE_LOCATION Resources
    )
    list(APPEND CARDSHOWCASE_MACOS_RESOURCES "${resource}")
  endforeach()
  add_executable(LokaCardShowcaseMacOS MACOSX_BUNDLE
    ${MACOS_SOURCES} ${MACOS_HEADERS} ${CARDSHOWCASE_MACOS_RESOURCES})
  loka_enable_warnings(LokaCardShowcaseMacOS)
  set_target_properties(LokaCardShowcaseMacOS PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.loka.cardshowcase"
    MACOSX_BUNDLE_BUNDLE_NAME "LokaCardShowcaseMacOS"
  )

  target_include_directories(LokaCardShowcaseMacOS PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apple/macos/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apple/macos/src/context
  )

  target_link_libraries(LokaCardShowcaseMacOS PRIVATE LokaMacOSCore)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND LOKA_MACOS_EXPLICIT_NO_ARC_FLAGS)
    target_compile_options(LokaCardShowcaseMacOS PRIVATE "-fno-objc-arc")
  endif()
endif()
