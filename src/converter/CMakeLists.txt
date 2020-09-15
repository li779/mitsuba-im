# COLLADA/OBJ importer (mtsimport)
if (NOT MTS_VERSION)
  message(FATAL_ERROR "Use the top level configuration file")
endif()

include_directories(${COLLADA_INCLUDE_DIRS} ${XERCES_INCLUDE_DIRS}
  ${CMAKE_CURRENT_BINARY_DIR})

set(SRCS
  converter.h
  converter.cpp
  obj.cpp
)

if (COLLADA_FOUND)
  set(SRCS ${SRCS} collada.cpp)
else()
  message(STATUS "Collada DOM was not found. The COLLADA importer will not be built.")
endif()

# COLLADA-DOM 2.4 introduces namespaces, which are not source-compatible
if (COLLADA_NAMESPACE)
  add_definitions(${COLLADA_DEFINITIONS} -DCOLLADA_DOM_2_4)
endif()

# Create a static library which is also used by mtsgui. Adding the dependent
# libraries will allow transitive linking.
add_library(mtsconverter_lib STATIC ${SRCS})
target_link_libraries(mtsconverter_lib
	${XERCES_LIBRARIES} ${OPENGL_glu_LIBRARY})
if (COLLADA_FOUND)
  target_link_libraries(mtsconverter_lib ${COLLADA_LIBRARIES})
endif ()

add_mts_exe(mtsimport mtsimport.cpp ${SRCS}
  ${MTS_DARWIN_STUB} ${MTS_WINDOWS_STUB} MTS_HW LINK_LIBRARIES mtsconverter_lib
  RES_DESCRIPTION "Mitsuba COLLADA 1.4 & Wavefront OBJ importer")
