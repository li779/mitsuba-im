# Cameras
if (NOT MTS_VERSION)
  message(FATAL_ERROR "Use the top level configuration file")
endif()

macro(add_sensor name)
  add_mts_plugin(${name} ${ARGN} TYPE sensor)
  target_sources(${name} PRIVATE mark_sensor.cpp)
endmacro()

add_sensor(radiancemeter      radiancemeter.cpp)
add_sensor(irradiancemeter    irradiancemeter.cpp)
add_sensor(fluencemeter       fluencemeter.cpp)
add_sensor(perspective        perspective.cpp)
add_sensor(perspective_rdist  perspective_rdist.cpp)
add_sensor(thinlens           thinlens.cpp)
add_sensor(orthographic       orthographic.cpp)
add_sensor(telecentric        telecentric.cpp)
add_sensor(spherical          spherical.cpp)
