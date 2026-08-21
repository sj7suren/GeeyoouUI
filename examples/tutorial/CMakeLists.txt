# The tutorial ships as two binaries built from ONE screen definition:
#
#   tutorial        the runnable result the reader ends up with (GUI)
#   tutorial_shots  renders each step offscreen to PNG, which is where the
#                   tutorial's figures come from
#
# Sharing ReactorScreen.cpp between them is the point: a screenshot in the docs
# cannot drift away from the code that produced it, because it IS the code that
# produced it.

add_executable(tutorial
  tutorial.cpp
  ReactorScreen.cpp
)
target_link_libraries(tutorial PRIVATE geeyoou::geeyoou)
target_include_directories(tutorial PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(tutorial PROPERTIES WIN32_EXECUTABLE TRUE)
if(MSVC)
  target_link_options(tutorial PRIVATE /ENTRY:mainCRTStartup)
  target_compile_options(tutorial PRIVATE /utf-8 /W4)
endif()

# A console program on purpose -- it prints what it wrote and its exit code is
# the verdict, so it can be wired into a docs job later.
add_executable(tutorial_shots
  shots.cpp
  ReactorScreen.cpp
)
target_link_libraries(tutorial_shots PRIVATE geeyoou::geeyoou)
target_include_directories(tutorial_shots PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
  target_compile_options(tutorial_shots PRIVATE /utf-8 /W4)
endif()

# Offscreen gallery for the new widgets -- a visual check target.
add_executable(widget_shots widget_shots.cpp)
target_link_libraries(widget_shots PRIVATE geeyoou::geeyoou)
target_include_directories(widget_shots PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
  target_compile_options(widget_shots PRIVATE /utf-8 /W4)
endif()

set_target_properties(tutorial tutorial_shots widget_shots PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
