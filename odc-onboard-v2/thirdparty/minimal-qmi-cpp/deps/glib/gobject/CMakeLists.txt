# libgobject-2.0 static library

# Generate glib-enumtypes from only the public glib headers (not gobject's own)
# Using pre-generated approach: run glib-mkenums at configure time with correct inputs
set(_mkenums ${CMAKE_CURRENT_SOURCE_DIR}/glib-mkenums.in)
set(_glib_src ${CMAKE_CURRENT_SOURCE_DIR}/../glib)

execute_process(
  COMMAND ${Python3_EXECUTABLE} ${_mkenums}
    --template ${CMAKE_CURRENT_SOURCE_DIR}/glib-enumtypes.h.template
    ${_glib_src}/gunicode.h
  OUTPUT_FILE ${DEPS_BUILD}/gobject/glib-enumtypes.h
)

execute_process(
  COMMAND ${Python3_EXECUTABLE} ${_mkenums}
    --template ${CMAKE_CURRENT_SOURCE_DIR}/glib-enumtypes.c.template
    ${_glib_src}/gunicode.h
  OUTPUT_FILE ${DEPS_BUILD}/gobject/glib-enumtypes.c
)

set(GOBJECT_SOURCES
  gatomicarray.c gbinding.c gbindinggroup.c gboxed.c gclosure.c genums.c
  gmarshal.c gobject.c gobjectnotifyqueue.c gparam.c gparamspecs.c
  gsignal.c gsignalgroup.c gsourceclosure.c gtype.c gtypemodule.c
  gtypeplugin.c gvalue.c gvaluearray.c gvaluetransform.c gvaluetypes.c
)
list(TRANSFORM GOBJECT_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

add_library(gobject-2.0 STATIC
  ${GOBJECT_SOURCES}
  ${DEPS_BUILD}/gobject/glib-enumtypes.c
)
add_dependencies(gobject-2.0 gobject-visibility)

target_include_directories(gobject-2.0
  PUBLIC
    ${DEPS_BUILD}/gobject
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(gobject-2.0 PRIVATE
  GOBJECT_COMPILATION
  G_LOG_DOMAIN="GLib-GObject"
  _GNU_SOURCE
)

target_link_libraries(gobject-2.0 PUBLIC glib-2.0 PkgConfig::FFI)
