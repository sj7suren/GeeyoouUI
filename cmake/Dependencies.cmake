# Blend2D + AsmJit, pinned to exact commits for reproducible builds.
#
# Blend2D's own CMakeLists expects to find AsmJit at ${ASMJIT_DIR} and calls
# add_subdirectory() on it itself.  So we fetch AsmJit *without* adding it
# (SOURCE_SUBDIR points at a path with no CMakeLists.txt, which makes
# FetchContent_MakeAvailable skip the add_subdirectory step), then hand the
# path to Blend2D.

include(FetchContent)

set(GEEYOOU_ASMJIT_COMMIT  0bd5787b54b575ed94bf32ac452153b34385c514)
set(GEEYOOU_BLEND2D_COMMIT 6dbc2cefbc996379e07104e34519a440b49b15d7)

FetchContent_Declare(asmjit
  GIT_REPOSITORY https://github.com/asmjit/asmjit.git
  GIT_TAG        ${GEEYOOU_ASMJIT_COMMIT}
  GIT_SHALLOW    OFF
  SOURCE_SUBDIR  _do_not_add_subdirectory_)
FetchContent_MakeAvailable(asmjit)

set(ASMJIT_DIR "${asmjit_SOURCE_DIR}" CACHE PATH "AsmJit source directory" FORCE)
set(BLEND2D_STATIC ON CACHE BOOL "" FORCE)
set(BLEND2D_TEST   OFF CACHE BOOL "" FORCE)

FetchContent_Declare(blend2d
  GIT_REPOSITORY https://github.com/blend2d/blend2d.git
  GIT_TAG        ${GEEYOOU_BLEND2D_COMMIT}
  GIT_SHALLOW    OFF)
FetchContent_MakeAvailable(blend2d)
