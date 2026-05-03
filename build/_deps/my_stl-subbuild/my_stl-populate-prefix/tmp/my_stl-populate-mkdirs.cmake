# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/home/xertion/Code/My_STL")
  file(MAKE_DIRECTORY "/home/xertion/Code/My_STL")
endif()
file(MAKE_DIRECTORY
  "/home/xertion/Code/Tiny_KV/build/_deps/my_stl-build"
  "/home/xertion/Code/Tiny_KV/build/_deps/my_stl-subbuild/my_stl-populate-prefix"
  "/home/xertion/Code/Tiny_KV/build/_deps/my_stl-subbuild/my_stl-populate-prefix/tmp"
  "/home/xertion/Code/Tiny_KV/build/_deps/my_stl-subbuild/my_stl-populate-prefix/src/my_stl-populate-stamp"
  "/home/xertion/Code/Tiny_KV/build/_deps/my_stl-subbuild/my_stl-populate-prefix/src"
  "/home/xertion/Code/Tiny_KV/build/_deps/my_stl-subbuild/my_stl-populate-prefix/src/my_stl-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/xertion/Code/Tiny_KV/build/_deps/my_stl-subbuild/my_stl-populate-prefix/src/my_stl-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/xertion/Code/Tiny_KV/build/_deps/my_stl-subbuild/my_stl-populate-prefix/src/my_stl-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
