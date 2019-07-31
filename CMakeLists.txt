# Experimental CMake file for Mitusba
cmake_minimum_required(VERSION 3.8.0 FATAL_ERROR)

# ===== Project setup =====

# Allow to override the default project name "mitsuba"
if (NOT DEFINED MTS_PROJECT_NAME)
  set(MTS_PROJECT_NAME "mitsuba")
endif()
if (NOT DEFINED MTS_TARGET_BINARIES_DIR)
  set(MTS_TARGET_BINARIES_DIR ${CMAKE_CURRENT_BINARY_DIR}/binaries/$<CONFIG>)
endif()

# Set CMAKE_BUILD_TYPE to RelWithDebInfo by default
if (DEFINED CMAKE_BUILD_TYPE AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING
    "Choose the type of build, options are: Debug, Release, RelWithDebInfo, MinSizeRel." FORCE)
endif()

# Enable folders for projects in Visual Studio
if (CMAKE_GENERATOR MATCHES "Visual Studio")
  set_property(GLOBAL PROPERTY USE_FOLDERS ON)
endif()

# Pre-compiled header support
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/external/CMakePCHCompiler")

project(${MTS_PROJECT_NAME} C CXX CXXPCH)

# ===== Build configuration =====

include (CMakeDependentOption)
include (CMakeParseArguments)
include (CheckCXXSourceCompiles)
 
# Tell cmake where to find the additional modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/data/cmake")
# Make sure the cmake-provided modules use the versions they expect
cmake_policy(SET CMP0017 NEW)
# Allow for quoted macro variable checks :-////
cmake_policy(SET CMP0054 NEW)

include (MtsGetVersionInfo)
# Read the version information
MTS_GET_VERSION_INFO()
if (MTS_HAS_VALID_REV)
  message(STATUS "mitsuba ${MTS_VERSION}-git${MTS_REV_ID} (${MTS_DATE})")
else()
  message(STATUS "mitsuba ${MTS_VERSION} (${MTS_DATE})")
endif()

# By default, keep module code separate to simplify debugging
cmake_policy(SET CMP0069 OLD)

include (MitsubaUtil)
include (MitsubaBuildOptions)

# ===== 3rd party dependencies =====

include(MitsubaExternal)

# ===== Prerequisite resources =====

# Main mitsuba include directory
include_directories("include")

# Process the XML schemas
add_subdirectory(data/schema)
# Add the IOR database
add_subdirectory(data/ior)
# Microfacet precomputed data
add_subdirectory(data/microfacet)

# ===== Build the support libraries ====

# Core support library
add_subdirectory(src/libcore)
# Rendering-related APIs
add_subdirectory(src/librender)
# Hardware acceleration
if (MTS_HAS_HW)
	add_subdirectory(src/libhw)
endif ()
# Bidirectional support library
add_subdirectory(src/libbidir)
# Python binding library
if (BUILD_PYTHON)
  add_subdirectory(src/libpython)
elseif(NOT PYTHON_FOUND)
  message(STATUS "Python was not found. The bindings will not be built.")
endif()


# Additional files to add to main executables
if(APPLE)
  set(MTS_DARWIN_STUB "${CMAKE_CURRENT_SOURCE_DIR}/src/mitsuba/darwin_stub.mm")
  set(MTS_WINDOWS_STUB "")
elseif(WIN32)
  set(MTS_DARWIN_STUB "")
  set(MTS_WINDOWS_STUB "${CMAKE_CURRENT_SOURCE_DIR}/data/windows/wmain_stub.cpp")
else()
  set(MTS_DARWIN_STUB "")
  set(MTS_WINDOWS_STUB "")
endif()


# ===== Build the applications =====

# Build the command-line binaries
add_subdirectory(src/mitsuba)

# Build the COLLADA converter
if (COLLADA_FOUND)
  add_subdirectory(src/converter)
else()
  message(STATUS "Collada DOM was not found. The importer will not be built.")
endif()

# Build the Qt-based GUI binaries
if (BUILD_GUI)
  add_subdirectory(src/mtsgui)
elseif(NOT QT4_FOUND)
  message(STATUS "Qt4 was not found. The mitsuba gui will not be built.")
endif()

# Build IM-MTS
if (BUILD_IMGUI)
	add_subdirectory(src/im-mts)
endif()

# ===== Build the plugins =====

# Utilities
#add_subdirectory(src/utils) #todo: convert to std c++
# Surface scattering models
add_subdirectory(src/bsdfs)
# Phase functions
add_subdirectory(src/phase)
# Intersection shapes
add_subdirectory(src/shapes)
# Sample generators
add_subdirectory(src/samplers)
# Reconstruction filters
add_subdirectory(src/rfilters)
# Film implementations
add_subdirectory(src/films)
# Sensors
add_subdirectory(src/sensors)
# Emitters
add_subdirectory(src/emitters)
# Participating media
add_subdirectory(src/medium)
# Volumetric data sources
add_subdirectory(src/volume)
# Sub-surface integrators
add_subdirectory(src/subsurface)
# Texture types
add_subdirectory(src/textures)
# Integrators
add_subdirectory(src/integrators)
# Testcases
if (BUILD_TESTS)
  add_subdirectory(src/tests)
endif()


# ===== Packaging =====

# Use a subdirectory to enforce that packaging runs after all other targets
add_subdirectory(data/cmake/packaging)
