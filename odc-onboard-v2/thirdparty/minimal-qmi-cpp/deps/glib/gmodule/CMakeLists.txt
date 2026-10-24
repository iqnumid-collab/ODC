# libgmodule-2.0 static library

add_library(gmodule-2.0 STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/gmodule.c
  ${CMAKE_CURRENT_SOURCE_DIR}/gmodule-deprecated.c
)
add_dependencies(gmodule-2.0 gmodule-visibility)

target_include_directories(gmodule-2.0
  PUBLIC
    ${DEPS_BUILD}/gmodule
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(gmodule-2.0 PRIVATE
  G_LOG_DOMAIN="GModule"
  GMODULE_COMPILATION
  _GNU_SOURCE
)

target_link_libraries(gmodule-2.0 PUBLIC glib-2.0 ${DL_LIBRARY})
