# libgthread-2.0 static library (stub in modern GLib)
add_library(gthread-2.0 STATIC ${CMAKE_CURRENT_SOURCE_DIR}/gthread-impl.c)

target_include_directories(gthread-2.0 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_compile_definitions(gthread-2.0 PRIVATE G_LOG_DOMAIN="GThread" _GNU_SOURCE)

target_link_libraries(gthread-2.0 PUBLIC glib-2.0)
