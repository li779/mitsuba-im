# main programs
if (NOT MTS_VERSION)
  message(FATAL_ERROR "Use the top level configuration file")
endif()

set(HDRS
  shell.h
  ../../external/imgui/imgui.h
  ../../external/tinyfiledialogs/tinyfiledialogs.h
  imgui_impl_opengl2.h
  )

set(SRCS
  mitsuba.cpp
  shell.cpp
  scene.cpp
  configurator.cpp
  preview_opengl2.cpp
  ../../external/imgui/imgui.cpp
  ../../external/imgui/imgui_draw.cpp
  ../../external/imgui/imgui_widgets.cpp
  ../../external/imgui/examples/imgui_impl_glfw.cpp 
  ../../external/imgui/misc/cpp/imgui_stdlib.cpp
  ../../external/tinyfiledialogs/tinyfiledialogs.c
  imgui_impl_opengl2.cpp
  )

include_directories(${GLFW_INCLUDE_DIRS} ../../external/imgui ../../external/tinyfiledialogs)

add_mts_exe(im-mts  ${SRCS} ${HDRS} 
  RES_DESCRIPTION "Mitsuba immediate-mode UI"
  LINK_LIBRARIES ${GLFW_LIBRARIES} ${OPENGL_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
