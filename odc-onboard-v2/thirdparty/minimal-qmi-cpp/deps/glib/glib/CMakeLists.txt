# libglib-2.0 static library

set(GLIB_CORE_SOURCES
  docs.c garcbox.c garray.c gasyncqueue.c gatomic.c gbacktrace.c gbase64.c
  gbitlock.c gbookmarkfile.c gbytes.c gcharset.c gchecksum.c gconvert.c
  gdataset.c gdate.c gdatetime.c gdatetime-private.c gdir.c genviron.c
  gerror.c gfileutils.c ggettext.c ghash.c ghmac.c ghook.c ghostutils.c
  giochannel.c giounix.c gjournal-private.c gkeyfile.c glib-init.c
  glib-private.c glib-unix.c glist.c gmain.c gmappedfile.c gmarkup.c gmem.c
  gmessages.c gnode.c goption.c gpathbuf.c gpattern.c gpoll.c gprimes.c
  gprint.c gprintf.c gqsort.c gquark.c gqueue.c grand.c grcbox.c
  grefcount.c grefstring.c gregex.c gscanner.c gsequence.c gshell.c
  gslice.c gslist.c gspawn.c gspawn-posix.c gstdio.c
  gstrfuncs.c gstring.c gstringchunk.c gstrvbuilder.c gtestutils.c
  gthread.c gthreadpool.c gthread-posix.c gtimer.c gtimezone.c gtrace.c
  gtranslit.c gtrashstack.c gtree.c gunibreak.c gunicollate.c gunidecomp.c
  guniprop.c guri.c gutf8.c gutils.c guuid.c gvariant.c
  gvariant-core.c gvariant-parser.c gvariant-serialiser.c gvarianttype.c
  gvarianttypeinfo.c gversion.c gwakeup.c
)
list(TRANSFORM GLIB_CORE_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

# gnulib - not needed on Linux with glibc (USE_SYSTEM_PRINTF)
set(GNULIB_SOURCES "")
set(LIBCHARSET_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/libcharset/localcharset.c")

# deprecated sources
file(GLOB DEPRECATED_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/deprecated/*.c")

# sysprof-capture (embedded in glib by meson)
set(SYSPROF_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../subprojects/sysprof-capture)
file(GLOB SYSPROF_SOURCES "${SYSPROF_DIR}/*.c")

add_library(glib-2.0 STATIC
  ${GLIB_CORE_SOURCES}
  ${GNULIB_SOURCES}
  ${LIBCHARSET_SOURCES}
  ${DEPRECATED_SOURCES}
  ${SYSPROF_SOURCES}
)

add_dependencies(glib-2.0 glib-visibility gversionmacros)

target_include_directories(glib-2.0
  PUBLIC
    ${DEPS_BUILD}          # config.h
    ${DEPS_BUILD}/glib     # glibconfig.h, gversionmacros.h, glib-visibility.h
    ${CMAKE_CURRENT_SOURCE_DIR}/..  # glib/ parent so #include <glib/xxx.h> works
    ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/gnulib
    ${CMAKE_CURRENT_SOURCE_DIR}/libcharset
    ${CMAKE_CURRENT_SOURCE_DIR}/deprecated
    ${CMAKE_CURRENT_SOURCE_DIR}/../subprojects/sysprof-capture
)

target_compile_definitions(glib-2.0
  PUBLIC
    GLIB_STATIC_COMPILATION
    GOBJECT_STATIC_COMPILATION
    GIO_STATIC_COMPILATION
    GMODULE_STATIC_COMPILATION
  PRIVATE
    GLIB_COMPILATION
    G_LOG_DOMAIN="GLib"
    _GNU_SOURCE
    LIBDIR="/usr/lib"
    LOCALEDIR=""
    HAVE_CONFIG_H
)

target_link_libraries(glib-2.0
  PUBLIC
    Threads::Threads
    PkgConfig::PCRE2
    ${M_LIBRARY}
    ${DL_LIBRARY}
    ${RT_LIBRARY}
)
