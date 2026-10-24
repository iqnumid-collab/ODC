# GLib umbrella CMakeLists.txt
include(${DEPS_ROOT}/cmake/GLibConfig.cmake)
include(${DEPS_ROOT}/cmake/GLibConfigTypes.cmake)

# Generate gmoduleconf.h
set(G_MODULE_IMPL "G_MODULE_IMPL_DL")
set(G_MODULE_HAVE_DLERROR 1)
set(G_MODULE_NEED_USCORE 0)
set(G_MODULE_BROKEN_RTLD_GLOBAL 0)
configure_file(
  ${DEPS_ROOT}/glib/cmake/gmoduleconf.h.cmake.in
  ${DEPS_BUILD}/gmodule/gmoduleconf.h
)

# Generate gnetworking.h (minimal)
configure_file(
  ${DEPS_ROOT}/glib/cmake/gnetworking.h.cmake.in
  ${DEPS_BUILD}/gio/gnetworking.h
)

# Ensure output directories exist
file(MAKE_DIRECTORY ${DEPS_BUILD}/glib)
file(MAKE_DIRECTORY ${DEPS_BUILD}/gobject)
file(MAKE_DIRECTORY ${DEPS_BUILD}/gio)
file(MAKE_DIRECTORY ${DEPS_BUILD}/gmodule)

# Generate proper visibility headers using the python script
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(_vis_script ${CMAKE_CURRENT_SOURCE_DIR}/gen-visibility-macros.py)
set(_glib_ver "${GLIB_MAJOR_VERSION}.${GLIB_MINOR_VERSION}.${GLIB_MICRO_VERSION}")

# The versions-macros subcommand needs an input .h.in template
# We'll use the source gversionmacros.h.in
set(_ver_in ${CMAKE_CURRENT_SOURCE_DIR}/glib/gversionmacros.h.in)

foreach(_lib glib gobject gio gmodule)
  string(TOUPPER ${_lib} _LIB)
  add_custom_command(
    OUTPUT ${DEPS_BUILD}/${_lib}/${_lib}-visibility.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DEPS_BUILD}/${_lib}
    COMMAND ${Python3_EXECUTABLE} ${_vis_script} ${_glib_ver} visibility-macros
      ${_LIB} ${DEPS_BUILD}/${_lib}/${_lib}-visibility.h
    DEPENDS ${_vis_script}
    COMMENT "Generating ${_lib}-visibility.h"
  )
  add_custom_target(${_lib}-visibility DEPENDS ${DEPS_BUILD}/${_lib}/${_lib}-visibility.h)
endforeach()

# Generate gversionmacros.h
add_custom_command(
  OUTPUT ${DEPS_BUILD}/glib/gversionmacros.h
  COMMAND ${CMAKE_COMMAND} -E make_directory ${DEPS_BUILD}/glib
  COMMAND ${Python3_EXECUTABLE} ${_vis_script} ${_glib_ver} versions-macros
    ${_ver_in} ${DEPS_BUILD}/glib/gversionmacros.h
  DEPENDS ${_vis_script} ${_ver_in}
  COMMENT "Generating gversionmacros.h"
)
add_custom_target(gversionmacros DEPENDS ${DEPS_BUILD}/glib/gversionmacros.h)

add_subdirectory(glib)
add_subdirectory(gobject)
add_subdirectory(gmodule)
add_subdirectory(gthread)
add_subdirectory(gio)

# Umbrella interface target
add_library(glib-complete INTERFACE)
target_link_libraries(glib-complete INTERFACE
  gio-2.0 gobject-2.0 gmodule-2.0 gthread-2.0 glib-2.0)
