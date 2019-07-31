# Film implementations
if (NOT MTS_VERSION)
  message(FATAL_ERROR "Use the top level configuration file")
endif()

macro(add_film)
  add_mts_plugin(${ARGN} TYPE film)
endmacro()

include_directories(${ZLIB_INCLUDE_DIRS})
add_film(mfilm   mfilm.cpp cnpy.h cnpy.cpp LINK_LIBRARIES ${ZLIB_LIBRARIES})
add_film(ldrfilm ldrfilm.cpp annotations.h banner.h)
add_film(hdrfilm hdrfilm.cpp annotations.h banner.h)

if (OPENEXR_FOUND)
  include_directories(${ILMBASE_INCLUDE_DIRS} ${OPENEXR_INCLUDE_DIRS})
  add_film(tiledhdrfilm tiledhdrfilm.cpp LINK_LIBRARIES ${OPENEXR_LIBRARIES})
endif()
