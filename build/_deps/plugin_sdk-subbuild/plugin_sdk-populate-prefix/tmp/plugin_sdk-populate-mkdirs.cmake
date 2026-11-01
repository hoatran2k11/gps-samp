# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "D:/Project/Code/game/GTA-GPS-Redux/build/_deps/plugin_sdk-src")
  file(MAKE_DIRECTORY "D:/Project/Code/game/GTA-GPS-Redux/build/_deps/plugin_sdk-src")
endif()
file(MAKE_DIRECTORY
  "D:/Project/Code/game/GTA-GPS-Redux/build/_deps/plugin_sdk-build"
  "D:/Project/Code/game/GTA-GPS-Redux/build/_deps/plugin_sdk-subbuild/plugin_sdk-populate-prefix"
  "D:/Project/Code/game/GTA-GPS-Redux/build/_deps/plugin_sdk-subbuild/plugin_sdk-populate-prefix/tmp"
  "D:/Project/Code/game/GTA-GPS-Redux/build/_deps/plugin_sdk-subbuild/plugin_sdk-populate-prefix/src/plugin_sdk-populate-stamp"
  "D:/Project/Code/game/GTA-GPS-Redux/build/_deps/plugin_sdk-subbuild/plugin_sdk-populate-prefix/src"
  "D:/Project/Code/game/GTA-GPS-Redux/build/_deps/plugin_sdk-subbuild/plugin_sdk-populate-prefix/src/plugin_sdk-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "D:/Project/Code/game/GTA-GPS-Redux/build/_deps/plugin_sdk-subbuild/plugin_sdk-populate-prefix/src/plugin_sdk-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "D:/Project/Code/game/GTA-GPS-Redux/build/_deps/plugin_sdk-subbuild/plugin_sdk-populate-prefix/src/plugin_sdk-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
