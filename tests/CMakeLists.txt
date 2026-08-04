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
  widget/test_tree.cpp
  widget/test_hittest.cpp
  render/test_style_cascade.cpp
  visual/test_golden.cpp
)

target_link_libraries(geeyoou_tests PRIVATE geeyoou::geeyoou)
target_include_directories(geeyoou_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

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
