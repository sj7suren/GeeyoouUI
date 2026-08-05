# Unit tests.
#
# One executable, no test-runner dependency: framework/Test.cpp supplies main().
# Note that it does NOT link blend2d -- Blend2D is PRIVATE to the library on
# purpose (see the root CMakeLists), and a test that reached for BL* names would
# be a third translation unit doing so.  Everything the visual tests need comes
# through geeyoou/render/Offscreen.hpp.

add_executable(geeyoou_tests
  framework/Test.cpp
  core/test_signal.cpp
  core/test_d7_assert.cpp
  core/test_connection_scope.cpp
  core/test_platform_timer.cpp
  core/test_tag_id.cpp
  widget/test_tree.cpp
  widget/test_hittest.cpp
  widget/test_removal.cpp
  widget/test_popup_lifetime.cpp
  widget/test_geometry_baseline.cpp
  widget/test_layout_engine.cpp
  widget/test_box_grid_layout.cpp
  widget/test_layout_alloc.cpp
  widget/test_size_hints.cpp
  widget/test_showcase_pages.cpp
  widget/test_r2_safety.cpp
  widget/test_r2_remediation.cpp
  widget/test_layout_soak.cpp
  render/test_style_cascade.cpp
  visual/test_golden.cpp

  # Not library code: Shell is the showcase's own frame.  It is compiled INTO
  # the test binary because it is one of the four relayout() implementations
  # every showcase page stands on, and a geometry regression in it would
  # otherwise only be visible by looking at the running application.
  ${CMAKE_SOURCE_DIR}/examples/showcase/Shell.cpp

  # Same argument, and now stated as a RULE rather than as a list: EVERY page
  # that hands its content to the layout engine is compiled in here.  The rule
  # replaces an enumeration ("the three pages R2 migrated") that silently went
  # stale the moment a fourth and fifth one landed -- PageIcons.cpp and
  # PageLayout.cpp shipped outside every gate, so ~900 lines standing on
  # BoxLayout/GridLayout/GroupBox/ScrollArea were never compiled at /W4
  # /permissive-, never asserted against, and never seen by ASan, which is the
  # only leg that can see the use-after-free family this codebase keeps
  # producing.
  #
  # The rule, so the next page does not repeat it: a page that calls setLayout()
  # belongs in this list.  The remaining five (Overview, Hmi, Selects, Window,
  # Theme) place their children with setGeometry() only -- absolute positioning
  # is a supported way to write a page, not a backlog item, and it exercises no
  # library geometry code that the widget tests do not already cover directly.
  ${CMAKE_SOURCE_DIR}/examples/showcase/PageWidgets.cpp
  ${CMAKE_SOURCE_DIR}/examples/showcase/PageInputs.cpp
  ${CMAKE_SOURCE_DIR}/examples/showcase/PageOps.cpp
  ${CMAKE_SOURCE_DIR}/examples/showcase/PageIcons.cpp
  ${CMAKE_SOURCE_DIR}/examples/showcase/PageLayout.cpp

  # PageIcons renders the registry, and half of what it renders -- the accent
  # tint, the corner dot, the `icons().find("name")` usage line -- is the
  # IconEntry::builtin == false branch, which no built-in can produce.  Only a
  # plant registration makes that branch reachable, so the plant icons come in
  # with the page or the page is only half testable.
  #
  # Note that linking this TU registers NOTHING on its own: registerPlantIcons()
  # has no static initialiser and must be called.  Tests that call it are
  # mutating the process-wide registry, so they own restoring/accounting for the
  # icon count other cases observe.
  ${CMAKE_SOURCE_DIR}/examples/showcase/PlantIcons.cpp
)

target_link_libraries(geeyoou_tests PRIVATE geeyoou::geeyoou)
target_include_directories(geeyoou_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/examples
  ${CMAKE_SOURCE_DIR}/examples/showcase)

# Baked in rather than resolved relative to the executable: the binary lives in
# build/bin and the baselines live in the source tree, and a test that guessed
# at that relationship would break the first time somebody used a different
# build directory.
target_compile_definitions(geeyoou_tests PRIVATE
  GEEYOOU_BASELINE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/visual/baseline")

if(MSVC)
  target_compile_options(geeyoou_tests PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
else()
  target_compile_options(geeyoou_tests PRIVATE -Wall -Wextra)
endif()

set_target_properties(geeyoou_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_test(NAME geeyoou_tests COMMAND geeyoou_tests)
