# Install script for directory: E:/Develop/tools/GeeyoouUI/build/_deps/blend2d-src

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "C:/Program Files (x86)/GeeyoouUI")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "RelWithDebInfo")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/Develop/tools/GeeyoouUI/build-asan/_deps/blend2d-build/asmjit/cmake_install.cmake")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "E:/Develop/tools/GeeyoouUI/build-asan/_deps/blend2d-build/blend2d.lib")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/blend2d/blend2d-targets.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/blend2d/blend2d-targets.cmake"
         "E:/Develop/tools/GeeyoouUI/build-asan/_deps/blend2d-build/CMakeFiles/Export/a5cf91491401f5de375ff0918bb48814/blend2d-targets.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/blend2d/blend2d-targets-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/blend2d/blend2d-targets.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/blend2d" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build-asan/_deps/blend2d-build/CMakeFiles/Export/a5cf91491401f5de375ff0918bb48814/blend2d-targets.cmake")
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/blend2d" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build-asan/_deps/blend2d-build/CMakeFiles/Export/a5cf91491401f5de375ff0918bb48814/blend2d-targets-relwithdebinfo.cmake")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/blend2d" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build-asan/_deps/blend2d-build/blend2d-config.cmake")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/blend2d" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build/_deps/blend2d-src/blend2d/blend2d.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/blend2d" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build/_deps/blend2d-src/blend2d/blend2d-debug.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/blend2d" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build/_deps/blend2d-src/blend2d/blend2d-impl.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/blend2d/core" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build/_deps/blend2d-src/blend2d/core/api.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/blend2d/core" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build/_deps/blend2d-src/blend2d/core/api-impl.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/blend2d/core" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build/_deps/blend2d-src/blend2d/core/array.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/blend2d/core" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build/_deps/blend2d-src/blend2d/core/bitarray.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/blend2d/core" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build/_deps/blend2d-src/blend2d/core/bitset.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/blend2d/core" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build/_deps/blend2d-src/blend2d/core/context.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/blend2d/core" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build/_deps/blend2d-src/blend2d/core/filesystem.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/blend2d/core" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build/_deps/blend2d-src/blend2d/core/font.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/blend2d/core" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build/_deps/blend2d-src/blend2d/core/fontdata.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/blend2d/core" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build/_deps/blend2d-src/blend2d/core/fontdefs.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/blend2d/core" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build/_deps/blend2d-src/blend2d/core/fontface.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/blend2d/core" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build/_deps/blend2d-src/blend2d/core/fontfeaturesettings.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/blend2d/core" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build/_deps/blend2d-src/blend2d/core/fontmanager.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/blend2d/core" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build/_deps/blend2d-src/blend2d/core/fontvariationsettings.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/blend2d/core" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build/_deps/blend2d-src/blend2d/core/format.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/blend2d/core" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build/_deps/blend2d-src/blend2d/core/geometry.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/blend2d/core" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build/_deps/blend2d-src/blend2d/core/glyphbuffer.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/blend2d/core" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build/_deps/blend2d-src/blend2d/core/glyphrun.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/blend2d/core" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build/_deps/blend2d-src/blend2d/core/gradient.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/blend2d/core" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build/_deps/blend2d-src/blend2d/core/image.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/blend2d/core" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build/_deps/blend2d-src/blend2d/core/imagecodec.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/blend2d/core" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build/_deps/blend2d-src/blend2d/core/imagedecoder.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/blend2d/core" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build/_deps/blend2d-src/blend2d/core/imageencoder.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/blend2d/core" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build/_deps/blend2d-src/blend2d/core/matrix.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/blend2d/core" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build/_deps/blend2d-src/blend2d/core/object.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/blend2d/core" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build/_deps/blend2d-src/blend2d/core/path.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/blend2d/core" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build/_deps/blend2d-src/blend2d/core/pattern.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/blend2d/core" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build/_deps/blend2d-src/blend2d/core/pixelconverter.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/blend2d/core" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build/_deps/blend2d-src/blend2d/core/random.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/blend2d/core" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build/_deps/blend2d-src/blend2d/core/rgba.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/blend2d/core" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build/_deps/blend2d-src/blend2d/core/runtime.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/blend2d/core" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build/_deps/blend2d-src/blend2d/core/runtimescope.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/blend2d/core" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build/_deps/blend2d-src/blend2d/core/string.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/blend2d/core" TYPE FILE FILES "E:/Develop/tools/GeeyoouUI/build/_deps/blend2d-src/blend2d/core/var.h")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "E:/Develop/tools/GeeyoouUI/build-asan/_deps/blend2d-build/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
