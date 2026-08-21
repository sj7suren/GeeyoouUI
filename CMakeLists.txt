cmake_minimum_required(VERSION 3.24)
project(GeeyoouUI VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(GEEYOOU_BUILD_EXAMPLES "Build example applications" ON)
option(GEEYOOU_BUILD_TESTS "Build unit tests" ON)

include(cmake/Dependencies.cmake)

# ---------------------------------------------------------------- library ----
set(GEEYOOU_SOURCES
  src/core/Types.cpp
  src/core/TagRegistry.cpp
  src/render/Painter.cpp
  src/render/Icon.cpp
  src/render/IconRegistry.cpp
  src/render/VectorPath.cpp
  src/render/Canvas.cpp
  src/render/StyleSheet.cpp
  src/render/Skin.cpp
  src/widget/Widget.cpp
  src/widget/Layout.cpp
  src/widget/BoxLayout.cpp
  src/widget/GridLayout.cpp
  src/widget/Window.cpp
  src/widget/WindowHeader.cpp
  src/widget/AppWindow.cpp
  src/widget/Label.cpp
  src/widget/PushButton.cpp
  src/widget/CheckBox.cpp
  src/widget/RadioButton.cpp
  src/widget/ToggleSwitch.cpp
  src/widget/Slider.cpp
  src/widget/ProgressBar.cpp
  src/widget/GroupBox.cpp
  src/widget/Dialog.cpp
  src/widget/TabView.cpp
  src/widget/NumericKeypad.cpp
  src/widget/Separator.cpp
  src/widget/SpinBox.cpp
  src/widget/LineEdit.cpp
  src/widget/TextArea.cpp
  src/widget/IconButton.cpp
  src/widget/PopupList.cpp
  src/widget/SelectBase.cpp
  src/widget/ComboBox.cpp
  src/widget/SearchableSelect.cpp
  src/widget/MultiSelect.cpp
  src/widget/TreeSelect.cpp
  src/widget/ScrollArea.cpp
  src/widget/ListView.cpp
  src/widget/TableView.cpp
  src/widget/TreeTableModel.cpp
  src/widget/TablePager.cpp
  src/widget/MenuButton.cpp
  src/widget/ContextMenu.cpp
  src/widget/Cascader.cpp
  src/widget/DatePicker.cpp
  src/scene3d/Mesh.cpp
  src/scene3d/Scene3D.cpp
  src/scene3d/View3D.cpp
  src/hmi/StatusLed.cpp
  src/hmi/Gauge.cpp
  src/hmi/Bargraph.cpp
  src/hmi/TrendChart.cpp
  src/hmi/DataHub.cpp
  src/hmi/AlarmList.cpp
)

if(WIN32)
  list(APPEND GEEYOOU_SOURCES src/platform/win32/Win32Platform.cpp)
else()
  message(FATAL_ERROR
    "GeeyoouUI v1 only ships the Win32 backend. "
    "The platform interface (include/geeyoou/platform/Platform.hpp) is ready for "
    "an X11/Cocoa implementation -- see docs/architecture.md section 2.")
endif()

add_library(geeyoou STATIC ${GEEYOOU_SOURCES})
add_library(geeyoou::geeyoou ALIAS geeyoou)

target_include_directories(geeyoou
  PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Blend2D is PRIVATE on purpose: the public headers never expose BL* types, so
# consumers are not forced to see them.  Painter.hpp keeps the BLContext behind
# an opaque pointer -- see docs/architecture.md section 3.5.
target_link_libraries(geeyoou PRIVATE blend2d::blend2d)

if(WIN32)
  # dwmapi: frameless windows extend their frame by one pixel to keep the OS
  # drop shadow -- see the WM_NCCALCSIZE block in Win32Platform.cpp.
  target_link_libraries(geeyoou PRIVATE user32 gdi32 shcore imm32 dwmapi)
  target_compile_definitions(geeyoou PRIVATE
    NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
endif()

if(MSVC)
  target_compile_options(geeyoou PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
else()
  target_compile_options(geeyoou PRIVATE -Wall -Wextra)
endif()

# ------------------------------------------------------------------ tests ----
if(GEEYOOU_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# --------------------------------------------------------------- examples ----
if(GEEYOOU_BUILD_EXAMPLES)
  # ONE entry point.  Every former standalone example is now a page inside the
  # showcase shell -- see examples/showcase/Pages.hpp.
  add_subdirectory(examples/showcase)

  # The exception, and it earns it: the tutorial is the ONE thing a newcomer is
  # supposed to be able to read top-to-bottom and run, without first
  # understanding a page/shell architecture.  It also generates the tutorial's
  # own figures, so the docs cannot drift from the code.
  add_subdirectory(examples/tutorial)
endif()
