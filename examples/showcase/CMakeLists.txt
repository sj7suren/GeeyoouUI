add_executable(showcase
  main.cpp
  Shell.cpp
  ShowcaseWindow.cpp
  PlantIcons.cpp
  # i18n: the runtime, then one translation unit per language.  A new language
  # is a new lang_xx_YY.cpp listed here plus one line in I18n.cpp's packs().
  i18n/I18n.cpp
  i18n/lang_zh_CN.cpp
  i18n/lang_en_US.cpp
  PageWindow.cpp
  PageTheme.cpp
  PageIcons.cpp
  PageOverview.cpp
  PageHmi.cpp
  PageLayout.cpp
  PageWidgets.cpp
  PageInputs.cpp
  PageSelects.cpp
  PageOps.cpp
  PageTables.cpp
  PageTablesTree.cpp
  PageScene3D.cpp
)
target_link_libraries(showcase PRIVATE geeyoou::geeyoou)
target_include_directories(showcase PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(showcase PROPERTIES WIN32_EXECUTABLE TRUE)
if(MSVC)
  target_link_options(showcase PRIVATE /ENTRY:mainCRTStartup)
  target_compile_options(showcase PRIVATE /utf-8 /W4)
endif()

set_target_properties(showcase PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
