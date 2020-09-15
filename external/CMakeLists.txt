cmake_minimum_required(VERSION 3.8.0 FATAL_ERROR)

# Additional Options: ZLIB_ROOT

###########################################################################
# Header-only

project(mitsuba-external C CXX)

if (MTS_BOOST_INSTALL_DIR)
	install(DIRECTORY boost-config/include/boost DESTINATION ${MTS_BOOST_INSTALL_DIR}/include)
	install(DIRECTORY boost-core/include/boost DESTINATION ${MTS_BOOST_INSTALL_DIR}/include)
	install(DIRECTORY boost-mpl/include/boost DESTINATION ${MTS_BOOST_INSTALL_DIR}/include)
	install(DIRECTORY boost-preprocessor/include/boost DESTINATION ${MTS_BOOST_INSTALL_DIR}/include)
	install(DIRECTORY boost-static_assert/include/boost DESTINATION ${MTS_BOOST_INSTALL_DIR}/include)
	install(DIRECTORY boost-type_traits/include/boost DESTINATION ${MTS_BOOST_INSTALL_DIR}/include)
	install(DIRECTORY boost-utility/include/boost DESTINATION ${MTS_BOOST_INSTALL_DIR}/include)
	#install(DIRECTORY boost-math/include/boost DESTINATION ${MTS_BOOST_INSTALL_DIR}/include)
endif ()

if (MTS_EIGEN_INSTALL_DIR)
	install(DIRECTORY eigen/Eigen DESTINATION ${MTS_EIGEN_INSTALL_DIR}/include)
endif ()

###########################################################################
# Build

macro (add_external_package SUBDIR)
	add_subdirectory("${SUBDIR}" ${ARGN})
	get_property(external_subdir_bin DIRECTORY "${SUBDIR}" PROPERTY BINARY_DIR)
endmacro ()

function (copy_library_targets INSTALL_DIR)
	foreach (library_target ${ARGN})
		set_property(TARGET ${library_target} PROPERTY EXCLUDE_FROM_ALL FALSE)
		install(FILES $<TARGET_LINKER_FILE:${library_target}> DESTINATION ${INSTALL_DIR} RENAME ${library_target}.alib)
		install(FILES $<TARGET_FILE:${library_target}> DESTINATION ${INSTALL_DIR})
		get_target_property(target_type ${library_target} TYPE)
		if (NOT target_type STREQUAL "STATIC_LIBRARY")
			if (NOT WIN32)
				install(FILES $<TARGET_SONAME_FILE:${library_target}> DESTINATION ${INSTALL_DIR})
				install(FILES $<TARGET_SONAME_FILE:${library_target}> DESTINATION ${MTS_TARGET_BINARIES_DIR})
			endif()
			install(FILES $<TARGET_FILE:${library_target}> DESTINATION ${MTS_TARGET_BINARIES_DIR})
		endif ()
	endforeach ()
endfunction ()

add_custom_target(PARTIAL_BUILD ALL)

macro (publish_external_targets PREFIX MAIN_TARGET)
	add_dependencies(PARTIAL_BUILD ${MAIN_TARGET} ${ARGN})
	string(TOUPPER ${PREFIX} upper_PREFIX)
	copy_library_targets(${MTS_${upper_PREFIX}_INSTALL_DIR}/lib ${MAIN_TARGET} ${ARGN})
endmacro()

###########################################################################

if (MTS_ZLIB_INSTALL_DIR)
	function (add_zlib MTS_ZLIB_DIR)
		set(BUILD_SHARED_LIBS FALSE)
		set(CMAKE_INSTALL_PREFIX ${MTS_ZLIB_INSTALL_DIR})
		set(CMAKE_POSITION_INDEPENDENT_CODE ON)
		
		set(ZLIB_TARGET_NAME zlibstatic)
		add_external_package(${MTS_ZLIB_DIR} EXCLUDE_FROM_ALL)
		publish_external_targets(ZLIB ${ZLIB_TARGET_NAME})

		install(FILES ${MTS_ZLIB_DIR}/zlib.h DESTINATION ${MTS_ZLIB_INSTALL_DIR}/include)
		install(FILES ${external_subdir_bin}/zconf.h DESTINATION ${MTS_ZLIB_INSTALL_DIR}/include)

		# for dependent targets (e.g. PNG->ZLIB)
		set(ZLIB_FOUND TRUE PARENT_SCOPE)
		set(ZLIB_VERSION_STRING 1.2.11 PARENT_SCOPE) # Unavailable ... ${zlib_VERSION}
		set(ZLIB_INCLUDE_DIR ${MTS_ZLIB_DIR} PARENT_SCOPE)
		set(ZLIB_INCLUDE_DIRS ${MTS_ZLIB_DIR} PARENT_SCOPE)
		set(ZLIB_LIBRARY ${ZLIB_TARGET_NAME} PARENT_SCOPE)
		set(ZLIB_LIBRARIES ${ZLIB_TARGET_NAME} PARENT_SCOPE)
	endfunction ()
	add_zlib(zlib)
	set(ZLIB_ROOT ${MTS_ZLIB_INSTALL_DIR} CACHE STRING "Root search directory for the LIB library.")
endif ()

function (find_mts_zlib_library)
	set(ZLIB_LIBRARY ${ZLIB_ROOT}/lib/zlibstatic.alib)
	if (EXISTS ${ZLIB_LIBRARY})
		set(ZLIB_LIBRARY ${ZLIB_LIBRARY} PARENT_SCOPE)
		set(ZLIB_LIBRARIES ${ZLIB_LIBRARY} PARENT_SCOPE)
	endif ()
endfunction ()

if (MTS_PNG_INSTALL_DIR)
	function (add_libpng MTS_PNG_DIR)
		set(PNG_SHARED OFF CACHE INTERNAL "")
		set(PNG_TESTS OFF CACHE INTERNAL "")
		set(CMAKE_INSTALL_PREFIX ${MTS_PNG_INSTALL_DIR})
		set(SKIP_INSTALL_ALL ON)
		set(CMAKE_POSITION_INDEPENDENT_CODE ON)
		set(ZLIB_ROOT ${ZLIB_ROOT})
		find_mts_zlib_library()

		add_external_package(${MTS_PNG_DIR} EXCLUDE_FROM_ALL)
		publish_external_targets(PNG png_static)

		install(FILES ${MTS_PNG_DIR}/png.h ${MTS_PNG_DIR}/pngconf.h DESTINATION ${MTS_PNG_INSTALL_DIR}/include)
		install(FILES ${external_subdir_bin}/pnglibconf.h DESTINATION ${MTS_PNG_INSTALL_DIR}/include)
	endfunction ()
	add_libpng(libpng)
endif ()

if (MTS_JPEG_INSTALL_DIR)
	function (add_libjpeg MTS_LIBJPEG_DIR)
		set(ENABLE_SHARED OFF CACHE INTERNAL "")
		set(WITH_SIMD OFF CACHE INTERNAL "")
		set(WITH_TURBOJPEG OFF CACHE INTERNAL "")
		set(CMAKE_INSTALL_PREFIX ${MTS_JPEG_INSTALL_DIR})
		set(CMAKE_POSITION_INDEPENDENT_CODE ON)

		add_external_package(${MTS_LIBJPEG_DIR} EXCLUDE_FROM_ALL)
		publish_external_targets(JPEG jpeg-static)

		install(FILES ${MTS_LIBJPEG_DIR}/jpeglib.h ${MTS_LIBJPEG_DIR}/jerror.h ${MTS_LIBJPEG_DIR}/jmorecfg.h DESTINATION ${MTS_JPEG_INSTALL_DIR}/include)
		install(FILES ${external_subdir_bin}/jconfig.h DESTINATION ${MTS_JPEG_INSTALL_DIR}/include)
	endfunction ()
	add_libjpeg(libjpeg-turbo)
endif ()

if (MTS_OPENEXR_INSTALL_DIR OR MTS_ILMBASE_INSTALL_DIR)
	function (add_ilmexr MTS_OPENEXR_SOURCE_DIR MTS_ILMBASE_DIR MTS_OPENEXR_DIR)
		if (NOT MTS_ILMBASE_INSTALL_DIR)
			set(OPENEXR_BUILD_ILMBASE OFF CACHE INTERNAL "")
		endif ()
		if (NOT MTS_OPENEXR_INSTALL_DIR)
			set(OPENEXR_BUILD_OPENEXR OFF CACHE INTERNAL "")
		endif ()
		set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "")
		set(OPENEXR_BUILD_BOTH_STATIC_SHARED OFF CACHE INTERNAL "")
		set(PYILMBASE_ENABLE OFF CACHE INTERNAL "")
		set(OPENEXR_VIEWERS_ENABLE OFF CACHE INTERNAL "")
		set(BUILD_TESTING OFF CACHE INTERNAL "")
		set(INSTALL_OPENEXR_EXAMPLES OFF CACHE INTERNAL "")
		set(INSTALL_OPENEXR_DOCS OFF CACHE INTERNAL "")

		set(CMAKE_INSTALL_PREFIX ${MTS_OPENEXR_INSTALL_DIR})
		set(CMAKE_POSITION_INDEPENDENT_CODE ON)
		set(ZLIB_ROOT ${ZLIB_ROOT})
		find_mts_zlib_library()

		add_external_package(${MTS_OPENEXR_SOURCE_DIR} EXCLUDE_FROM_ALL)

		if (MTS_ILMBASE_INSTALL_DIR)
			publish_external_targets(ILMBASE Half)
			install(DIRECTORY ${MTS_ILMBASE_DIR}/Half/ DESTINATION ${MTS_ILMBASE_INSTALL_DIR}/include FILES_MATCHING PATTERN *.h)
			install(FILES ${external_subdir_bin}/IlmBase/config/IlmBaseConfig.h DESTINATION ${MTS_ILMBASE_INSTALL_DIR}/include)
		endif ()

		if (MTS_OPENEXR_INSTALL_DIR)
			publish_external_targets(OPENEXR IlmImf Imath Iex IlmThread)
			install(DIRECTORY ${MTS_ILMBASE_DIR}/Iex/ ${MTS_ILMBASE_DIR}/IexMath/ DESTINATION ${MTS_OPENEXR_INSTALL_DIR}/include/OpenEXR FILES_MATCHING PATTERN *.h)
			install(DIRECTORY ${MTS_ILMBASE_DIR}/Imath/ ${MTS_ILMBASE_DIR}/IlmThread/  DESTINATION ${MTS_OPENEXR_INSTALL_DIR}/include/OpenEXR FILES_MATCHING PATTERN *.h)
			install(DIRECTORY ${MTS_OPENEXR_DIR}/IlmImf/ DESTINATION ${MTS_OPENEXR_INSTALL_DIR}/include/OpenEXR FILES_MATCHING PATTERN *.h)
			install(FILES ${external_subdir_bin}/OpenEXR/config/OpenEXRConfig.h DESTINATION ${MTS_OPENEXR_INSTALL_DIR}/include/OpenEXR)
		endif ()
	endfunction ()
	add_ilmexr(openexr openexr/IlmBase openexr/OpenEXR)
endif()

if (MTS_XERCESC_INSTALL_DIR)
	function (add_xerces MTS_XERCESC_DIR)
		set(MTS_CACHED_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
		set(BUILD_SHARED_LIBS OFF CACHE BOOL "Static Xerces-C")
		set(network OFF CACHE INTERNAL "")
		set(threads OFF CACHE INTERNAL "")
		set(CMAKE_INSTALL_PREFIX ${MTS_XERCESC_INSTALL_DIR})
		set(CMAKE_POSITION_INDEPENDENT_CODE ON)
		
		add_external_package(${MTS_XERCESC_DIR} EXCLUDE_FROM_ALL)
		publish_external_targets(XercesC xerces-c)
		# reset
		set(BUILD_SHARED_LIBS ${MTS_CACHED_BUILD_SHARED_LIBS} CACHE BOOL "Build shared libs by default?")

		install(DIRECTORY ${MTS_XERCESC_DIR}/src/ DESTINATION ${MTS_XERCESC_INSTALL_DIR}/include FILES_MATCHING PATTERN *.hpp)
		install(DIRECTORY ${MTS_XERCESC_DIR}/src/xercesc/util/ DESTINATION ${MTS_XERCESC_INSTALL_DIR}/include/xercesc/util FILES_MATCHING PATTERN *.c)
		install(FILES ${external_subdir_bin}/src/xercesc/util/Xerces_autoconf_config.hpp
			          ${external_subdir_bin}/src/xercesc/util/XercesVersion.hpp
			DESTINATION ${MTS_XERCESC_INSTALL_DIR}/include/xercesc/util)
	endfunction ()
	add_xerces(xerces-c)
endif ()

if (MTS_GLFW_INSTALL_DIR)
	function (add_glfw MTS_GLFW_DIR)
		set(BUILD_SHARED_LIBS ON)
		set(GLFW_BUILD_EXAMPLES OFF CACHE INTERNAL "")
		set(GLFW_BUILD_TESTS OFF CACHE INTERNAL "")
		set(GLFW_BUILD_DOCS OFF CACHE INTERNAL "")
		set(CMAKE_INSTALL_PREFIX ${MTS_GLFW_INSTALL_DIR})

		add_external_package(${MTS_GLFW_DIR} EXCLUDE_FROM_ALL)
		publish_external_targets(GLFW glfw)

		install(DIRECTORY ${MTS_GLFW_DIR}/include/ DESTINATION ${MTS_GLFW_INSTALL_DIR}/include)
		install(FILES ${external_subdir_bin}/src/glfw_config.h DESTINATION ${MTS_GLFW_INSTALL_DIR}/include/GLFW)
	endfunction ()
	add_glfw(glfw)
endif ()

