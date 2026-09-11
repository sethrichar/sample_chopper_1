cmake_minimum_required(VERSION 3.22)
project(SampleChopper VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(SC_BUILD_CLI     "Build the command-line chopper"                 ON)
option(SC_BUILD_TESTS   "Build unit tests"                               ON)
option(SC_BUILD_PLUGIN  "Build the JUCE plugin (VST3 / AU / Standalone)" ON)
option(SC_COPY_PLUGIN_AFTER_BUILD "Install the plugin into the user plugin folders after building" OFF)

# The DSP + export core. No JUCE dependency; shared by CLI, tests and plugin.
add_subdirectory(core)

if(SC_BUILD_CLI)
  add_subdirectory(cli)
endif()

if(SC_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(SC_BUILD_PLUGIN)
  add_subdirectory(plugin)
endif()
