###############################################################################
#                EXTERNAL LIBRARIES DETECTION                                 #
###############################################################################

# Set up CMake to use the Mitsuba bundled libraries. Set the variable
# "MTS_NO_DEPENDENCIES" to a value which evaluates to TRUE to avoid
# using the Mitsuba dependencies even if they are present.
if (MSVC)
  set(MTS_DEPS_SUFFIX "_windows")
else()
  set(MTS_DEPS_SUFFIX "_macos")
endif()
set(MTS_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dependencies${MTS_DEPS_SUFFIX}")
if(NOT IS_DIRECTORY "${MTS_DEPS_DIR}")
  set(MTS_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dependencies")
endif()

if((MSVC OR APPLE) AND NOT MTS_NO_DEPENDENCIES AND
   IS_DIRECTORY "${MTS_DEPS_DIR}")
  set(MTS_DEPENDENCIES ON)
  set(CMAKE_PROGRAM_PATH "${MTS_DEPS_DIR}/bin")
  set(CMAKE_INCLUDE_PATH "${MTS_DEPS_DIR}/include")
  set(Boost_NO_SYSTEM_PATHS TRUE)
  
  if (MSVC)
    math(EXPR MTS_MSVC_VERSION "(${MSVC_VERSION} - 600) / 100")
    set(MTS_MSVC_VERSION "vc${MTS_MSVC_VERSION}")
    if(CMAKE_CL_64)
      set(MTS_PLATFORM "x64_${MTS_MSVC_VERSION}")
    else()
      set(MTS_PLATFORM "i386_${MTS_MSVC_VERSION}")
    endif()
    list(APPEND CMAKE_INCLUDE_PATH "${MTS_DEPS_DIR}/qt/include")
    set(CMAKE_LIBRARY_PATH "${MTS_DEPS_DIR}/lib/${MTS_PLATFORM}/"
      "${MTS_DEPS_DIR}/qt/${MTS_PLATFORM}/lib/")
    set(QT_BINARY_DIR "${MTS_DEPS_DIR}/qt/${MTS_PLATFORM}/bin")
  elseif(APPLE)
    set(CMAKE_LIBRARY_PATH   "${MTS_DEPS_DIR}/lib")
    set(CMAKE_FRAMEWORK_PATH "${MTS_DEPS_DIR}/frameworks")
    set(QT_BINARY_DIR        "${MTS_DEPS_DIR}/bin")
    # Create a shell script to set the paths for dyld
    file(WRITE "${PROJECT_BINARY_DIR}/binaries/mitsuba_dyld.sh"
"#!/bin/sh
# DYLD paths for the mitsuba dependencies. Created automatically by CMake.
export DYLD_FALLBACK_FRAMEWORK_PATH=\"${MTS_DEPS_DIR}/frameworks\":$DYLD_FALLBACK_FRAMEWORK_PATH
export DYLD_FALLBACK_LIBRARY_PATH=\"${MTS_DEPS_DIR}/lib\":$DYLD_FALLBACK_LIBRARY_PATH
")
  endif()
  # Check the standard configurations
  # NO Debug! Does not work under windows as dependencies do not include debug libraries
  mts_check_configurations (Release MinSizeRel RelWithDebInfo)
else()
  set(MTS_DEPENDENCIES OFF)
  unset(MTS_DEPS_DIR)
endif()

###########################################################################
# Embed external projects
include (ExternalProject)
set (MTS_EXTERNAL_PROJECT_VARS )

function (build_externals EXTERNALS_NAME DEPENDEES)
	set (MTS_EXTERNAL_PROJECT_VAR_EXPORT_LIST -DCMAKE_POLICY_DEFAULT_CMP0074=NEW) # don't warn about <Lib>_ROOT
	file(TO_CMAKE_PATH "${MTS_TARGET_BINARIES_DIR}/${MTS_LIB_DEST}" MTS_TARGET_BINARIES_DIR)
	foreach (external_var ${MTS_EXTERNAL_PROJECT_VARS} MTS_TARGET_BINARIES_DIR CMAKE_BUILD_TYPE)
		if (${external_var})
			list(APPEND MTS_EXTERNAL_PROJECT_VAR_EXPORT_LIST -D${external_var}=${${external_var}})
		endif ()
	endforeach ()
	# installation (not used yet)
	if (NOT CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
		list(APPEND MTS_EXTERNAL_PROJECT_VAR_EXPORT_LIST -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX})
	endif ()
	# include as part of the build process
	ExternalProject_Add(${EXTERNALS_NAME} PREFIX external/${EXTERNALS_NAME}
		SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external
		CMAKE_ARGS ${MTS_EXTERNAL_PROJECT_VAR_EXPORT_LIST}
		DEPENDS ${ARGN})
	set_property(TARGET ${EXTERNALS_NAME} PROPERTY FOLDER external)
	foreach (external_dependee ${DEPENDEES})
		add_dependencies(${external_dependee} ${EXTERNALS_NAME})
	endforeach ()
	# flushed ...
	set(MTS_EXTERNAL_PROJECT_VARS PARENT_SCOPE)
endfunction ()

###########################################################################
# Required
set (MTS_EXTERNAL_INTERFACE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/interface) 

# System threading library, used for custom options
set(CMAKE_THREAD_PREFER_PTHREAD ON)
find_package(Threads REQUIRED)

function (decorate_external_libraries OUT_VAR INSTALL_DIR)
	set (DECORATED_TARGETS )
	foreach (lib_target ${ARGN})
		list(APPEND DECORATED_TARGETS "${INSTALL_DIR}/${lib_target}.alib")
	endforeach ()
	set (${OUT_VAR} ${DECORATED_TARGETS} PARENT_SCOPE)
endfunction ()

function (add_imported_libraries TARGET HEAD)
	set_target_properties(${TARGET} PROPERTIES IMPORTED_LOCATION "${HEAD}")
	set(OTHERS ${ARGN})
	if (OTHERS)
		target_link_libraries(${TARGET} INTERFACE ${OTHERS})
	endif ()
endfunction ()

macro (make_external_library PREFIX MAIN_TARGET)
	string(TOUPPER ${PREFIX} upper_PREFIX)

	set(MTS_${upper_PREFIX}_INSTALL_DIR ${MTS_EXTERNAL_INTERFACE_DIR}/${PREFIX})
	list(APPEND MTS_EXTERNAL_PROJECT_VARS MTS_${upper_PREFIX}_INSTALL_DIR)

	set(${PREFIX}_ROOT ${MTS_${upper_PREFIX}_INSTALL_DIR} CACHE PATH
		"Preferred installation prefix for searching for ${PREFIX}.")
	set(${PREFIX}_FOUND TRUE)
	set(${upper_PREFIX}_FOUND TRUE)
	set(${PREFIX}_INCLUDE_DIRS ${${PREFIX}_ROOT}/include)

	set(${PREFIX}_LIBRARY ${MAIN_TARGET}-import)
	if ("${ARGN}" STREQUAL "HEADER_ONLY")
		add_library(${${PREFIX}_LIBRARY} INTERFACE)		
	else ()
		add_library(${${PREFIX}_LIBRARY} STATIC IMPORTED)
		decorate_external_libraries(${PREFIX}_LIBRARIES ${${PREFIX}_ROOT}/lib  ${MAIN_TARGET} ${ARGN})
		add_imported_libraries(${${PREFIX}_LIBRARY} ${${PREFIX}_LIBRARIES})
	endif ()
	set(${PREFIX}_LIBRARIES ${${PREFIX}_LIBRARY})
endmacro()

# Boost
if (MTS_ENABLE_SYSTEM_LIBS)
	find_package(Boost 1.44)
	mark_as_advanced(Boost_LIB_DIAGNOSTIC_DEFINITIONS)
endif ()
if (NOT Boost_FOUND)
	make_external_library(Boost boost HEADER_ONLY)
	# can run in parallel
	build_externals(boost "${Boost_LIBRARIES}")
endif()
# boost python disabled for now (todo: replace by sth else)
set (mts_boost_PYTHON_FOUND OFF)

# Eigen
if (MTS_ENABLE_SYSTEM_LIBS)
	find_package(Eigen 3.0)
endif ()
if (NOT Eigen_FOUND)
	make_external_library(Eigen eigen HEADER_ONLY)
	# can run in parallel
	build_externals(eigen "${Eigen_LIBRARIES}")
endif ()

# JPEG
if (MTS_ENABLE_SYSTEM_LIBS)
	find_package(JPEG 6)
endif ()
if (NOT JPEG_FOUND)
	make_external_library(JPEG jpeg-static)
	# can run in parallel
	build_externals(libjpeg "${JPEG_LIBRARIES}")
endif ()
# legacy fixup
if (JPEG_INCLUDE_DIR AND NOT JPEG_INCLUDE_DIRS)
	set (JPEG_INCLUDE_DIRS ${JPEG_INCLUDE_DIR})
endif ()

# ZLIB
if (MTS_ENABLE_SYSTEM_LIBS)
	find_package(ZLIB 1.2)
endif ()
if (NOT ZLIB_FOUND)
	make_external_library(ZLIB zlibstatic)
	# can run in parallel
	build_externals(zlib "${ZLIB_LIBRARIES}")
endif ()
# PNG
if (MTS_ENABLE_SYSTEM_LIBS)
	find_package(PNG 1.2)
endif ()
if (NOT PNG_FOUND)
	make_external_library(PNG png_static)
	set(PNG_DEFINITIONS -DPNG_STATIC)
	target_link_libraries(${PNG_LIBRARY} INTERFACE ${ZLIB_LIBRARIES})
	# can run in parallel
	list(APPEND MTS_EXTERNAL_PROJECT_VARS ZLIB_ROOT)
	build_externals(libpng "${PNG_LIBRARIES}" zlib)
endif ()
# legacy fixup
if (PNG_INCLUDE_DIR AND NOT PNG_INCLUDE_DIRS)
	set (PNG_INCLUDE_DIRS ${PNG_INCLUDE_DIR})
endif ()
add_definitions(${PNG_DEFINITIONS})
# OpenEXR
if (MTS_ENABLE_SYSTEM_LIBS)
	find_package(IlmBase)
	find_package(OpenEXR)
endif ()
if (NOT ILMBASE_FOUND OR NOT OPENEXR_FOUND)
	if (NOT ILMBASE_FOUND)
		make_external_library(ILMBASE Half)
	endif ()
	if (NOT OPENEXR_FOUND)
		make_external_library(OPENEXR IlmImf Iex IlmThread Imath)
	endif ()
	target_link_libraries(${OPENEXR_LIBRARY} INTERFACE ${ILMBASE_LIBRARIES} ${ZLIB_LIBRARIES})
	# can run in parallel
	list(APPEND MTS_EXTERNAL_PROJECT_VARS ZLIB_ROOT)
	build_externals(openexr "${ILMBASE_LIBRARIES};${OPENEXR_LIBRARIES}" zlib)
else ()
	if (WIN32)
	  set(CMAKE_REQUIRED_INCLUDES ${ILMBASE_INCLUDE_DIRS} ${OPENEXR_INCLUDE_DIRS})
	  set(CMAKE_REQUIRED_LIBRARIES ${ILMBASE_LIBRARIES} ${OPENEXR_LIBRARIES})

	  CHECK_CXX_SOURCE_COMPILES("
	#define OPENEXR_DLL
	#include <OpenEXR/half.h>
	#include <OpenEXR/ImfRgbaFile.h>
	int main(int argc, char **argv) {
		half x = 1.5f;
		Imf::RgbaInputFile file(static_cast<const char*>(0));
		file.readPixels(0,0);
		return x > 0 ? 0 : 1;
	}
	" OPENEXR_IS_DLL)

	  unset (CMAKE_REQUIRED_INCLUDES)
	  unset (CMAKE_REQUIRED_LIBRARIES)
	  
	  if (OPENEXR_IS_DLL)
		add_definitions(-DOPENEXR_DLL)
	  endif()
	endif()
endif()

# XercesC
if (MTS_ENABLE_SYSTEM_LIBS)
	find_package(XercesC 3.0)
endif ()
if (NOT XercesC_FOUND)
	make_external_library(XercesC xerces-c)
	# can run in parallel
	build_externals(xerces-c "${XercesC_LIBRARIES}")

	set(XercesC_DEFINITIONS -DXERCES_STATIC)
	if (NOT WIN32)
		target_link_libraries(${XercesC_LIBRARY} INTERFACE icuuc icudata)
	endif ()

	set(XERCES_DEFINITIONS ${XercesC_DEFINITIONS})
	set(XERCES_INCLUDE_DIR ${XercesC_INCLUDE_DIR})
	set(XERCES_INCLUDE_DIRS ${XercesC_INCLUDE_DIRS})
	set(XERCES_LIBRARY ${XercesC_LIBRARY})
	set(XERCES_LIBRARIES ${XercesC_LIBRARIES})
endif ()

###########################################################################
# Optional

# ColladaDOM (optional)
if (MTS_ENABLE_COLLADA)

find_package(COLLADA)
if (COLLADA_FOUND)
  add_definitions(-DMTS_HAS_COLLADA=1)
endif()

endif()

# FFTW3 (optional)
find_package(FFTW3)
CMAKE_DEPENDENT_OPTION(MTS_FFTW "Enable FFTW3 for fast image convolution support." ON
  "FFTW3_FOUND" OFF)
if (MTS_FFTW)
  add_definitions(-DMTS_HAS_FFTW=1)
endif()

# todo: python bindings disabled, depend on boost python right now
if (mts_boost_PYTHON_FOUND)
# The Python libraries. When using the built-in dependencies we need
# to specify the include directory, otherwise CMake finds the one
# from the local installation using the Windows registry / OSX Frameworks
if (MTS_DEPENDENCIES AND NOT PYTHON_INCLUDE_DIR AND
    EXISTS "${MTS_DEPS_DIR}/include/python27")
  set(PYTHON_INCLUDE_DIR "${MTS_DEPS_DIR}/include/python27"
      CACHE STRING "Path to the Python include directory.")
endif()
find_package (PythonLibs "2.6")
CMAKE_DEPENDENT_OPTION(BUILD_PYTHON "Build the Python bindings." ON
  "PYTHONLIBS_FOUND;mts_boost_PYTHON_FOUND" OFF)
if (PYTHONLIBS_FOUND AND mts_boost_PYTHON_FOUND)
  set (PYTHON_FOUND TRUE)
endif ()
endif ()

###########################################################################
# Frontends & HW-accelerated rendering

# Qt frontend unsupported for now
if (MTS_ENABLE_QT)
	# Qt4 (optional)
	find_package(Qt4 4.7 COMPONENTS
	  QtCore QtGui QtXml QtXmlPatterns QtNetwork QtOpenGL)
	CMAKE_DEPENDENT_OPTION(BUILD_GUI "Built the Qt4-based mitsuba GUI." ON
	  "QT4_FOUND" OFF)
endif () 

# OpenGL
find_package(OpenGL)
CMAKE_DEPENDENT_OPTION(BUILD_IMGUI "Build the GL-based mitsuba GUI." ON
  "OPENGL_FOUND" OFF)

set (MTS_HAS_HW FALSE)

# disable HW-accelerated VPL preview for now
if (MTS_ENABLE_HW_PREVIEW)

set (GLEW_MX ON)
find_package(GLEW)
if (GLEW_FOUND)
  set (GLEW_STATE_VARS ${GLEW_INCLUDE_DIRS} ${GLEW_LIBRARIES})
  if (NOT GLEW_TEST_STATE)
    set (GLEW_TEST_STATE "${GLEW_STATE_VARS}" CACHE INTERNAL "GLEW State")
  endif ()
  if (NOT GLEW_TEST_STATE STREQUAL "${GLEW_STATE_VARS}")
    set (GLEW_TEST_STATE "${GLEW_STATE_VARS}" CACHE INTERNAL "GLEW State" FORCE)
    unset (GLEW_VERSION_IS_OK CACHE)
  endif ()
  set (CMAKE_REQUIRED_INCLUDES  ${GLEW_INCLUDE_DIRS})
  set (CMAKE_REQUIRED_LIBRARIES ${GLEW_LIBRARIES})
  CHECK_CXX_SOURCE_COMPILES("
#if defined(__APPLE__)
#include <OpenGL/glew.h>
#else
#include <GL/glew.h>
#endif
int main (int argc, char **argv) {
    int i = GL_VERTEX_ATTRIB_ARRAY_UNIFIED_NV;
    return 0;
}
" GLEW_VERSION_IS_OK)
  if (NOT GLEW_VERSION_IS_OK)
	  #    message (SEND_ERROR "The version of GLEW seems to be outdated!") XXX
  endif ()
endif ()

set (MTS_HAS_HW ${GLEW_FOUND})

endif ()

if (MTS_ENABLE_SYSTEM_LIBS)
	find_package(SDL 2.0)
endif ()
if (NOT SDL_FOUND)
	make_external_library(SDL SDL2 SDL2main)
endif ()

# can run in parallel
build_externals(sdl "${SDL_LIBRARIES}")

###########################################################################
# System libraries

# Try to get OpenMP support
find_package(OpenMP)
CMAKE_DEPENDENT_OPTION(MTS_OPENMP "Enable OpenMP support" ON
  "OPENMP_FOUND" OFF)
if (MTS_OPENMP)
  set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
else ()
  add_definitions (-DMTS_NO_OPENMP)
endif()

# Linux requires X11
if (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
  find_package(X11)
  if (NOT X11_xf86vmode_FOUND)
	  message(WARNING "X11 vmode was not found.")
  endif()
endif()

# Mac OS X Frameworks
if (APPLE)
  find_library(COCOA_LIBRARY Cocoa)
  find_library(BWTOOLKIT_LIBRARY BWToolkitFramework)
  find_path(BWTOOLKIT_INCLUDE_DIR BWToolkitFramework/BWToolkitFramework.h)
  find_library (SECURITY_LIBRARY Security 
    NO_CMAKE_ENVIRONMENT_PATH NO_CMAKE_PATH)
  find_path(SECURITY_INCLUDE_DIR "Authorization.h"
    NO_CMAKE_ENVIRONMENT_PATH NO_CMAKE_PATH)
  mark_as_advanced (COCOA_LIBRARY)
  mark_as_advanced (BWTOOLKIT_LIBRARY BWTOOLKIT_INCLUDE_DIR)
  mark_as_advanced (SECURITY_LIBRARY  SECURITY_INCLUDE_DIR)
endif()

###########################################################################
# Global configuration

# Includes for the common libraries
include_directories(${Boost_INCLUDE_DIRS})
link_libraries(${Boost_LIBRARIES}) # for build order only

if (EIGEN_FOUND)
  add_definitions(-DHAS_EIGEN=1)
  include_directories(${Eigen_INCLUDE_DIRS})
  link_libraries(${Eigen_LIBRARIES}) # for build order only
endif()

# If we are using the system OpenEXR, add its headers which half.h requires
if (ILMBASE_FOUND)
  include_directories(${ILMBASE_INCLUDE_DIRS})
endif()

# Image format definitions
if (PNG_FOUND)
  add_definitions(-DMTS_HAS_LIBPNG=1)
endif()
if (JPEG_FOUND)
  add_definitions(-DMTS_HAS_LIBJPEG=1)
endif()
if (OPENEXR_FOUND)
  add_definitions(-DMTS_HAS_OPENEXR=1)
endif()

# Features
if (MTS_HAS_HW)
  add_definitions(-DMTS_HAS_HW=1)
endif()
