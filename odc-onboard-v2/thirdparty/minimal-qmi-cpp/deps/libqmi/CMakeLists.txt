# libqmi-glib static library with codegen

set(QMI_MAJOR_VERSION 1)
set(QMI_MINOR_VERSION 38)
set(QMI_MICRO_VERSION 0)
set(QMI_MBIM_QMUX_SUPPORTED 1)
set(QMI_QRTR_SUPPORTED 0)
set(QMI_RMNET_SUPPORTED 0)

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/src/libqmi-glib/qmi-version.h.in
  ${DEPS_BUILD}/libqmi/qmi-version.h
  @ONLY
)

set(QMI_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/libqmi-glib)
set(QMI_GEN ${DEPS_BUILD}/libqmi/generated)
set(QMI_DATA ${CMAKE_CURRENT_SOURCE_DIR}/data)
set(QMI_CODEGEN ${CMAKE_CURRENT_SOURCE_DIR}/build-aux/qmi-codegen/qmi-codegen)
set(QMI_MKENUMS ${CMAKE_CURRENT_SOURCE_DIR}/build-aux/qmi-mkenums)
set(QMI_TEMPLATES ${CMAKE_CURRENT_SOURCE_DIR}/build-aux/templates)
set(QMI_COMMON ${QMI_DATA}/qmi-common.json)

file(MAKE_DIRECTORY ${QMI_GEN})

# Enum headers for mkenums
set(QMI_ENUMS_HEADERS
  ${QMI_SRC}/qmi-enums.h ${QMI_SRC}/qmi-enums-wds.h ${QMI_SRC}/qmi-enums-dms.h
  ${QMI_SRC}/qmi-enums-nas.h ${QMI_SRC}/qmi-enums-wms.h ${QMI_SRC}/qmi-enums-pds.h
  ${QMI_SRC}/qmi-enums-pdc.h ${QMI_SRC}/qmi-enums-pbm.h ${QMI_SRC}/qmi-enums-uim.h
  ${QMI_SRC}/qmi-enums-sar.h ${QMI_SRC}/qmi-enums-oma.h ${QMI_SRC}/qmi-enums-wda.h
  ${QMI_SRC}/qmi-enums-voice.h ${QMI_SRC}/qmi-enums-loc.h ${QMI_SRC}/qmi-enums-qos.h
  ${QMI_SRC}/qmi-enums-gas.h ${QMI_SRC}/qmi-enums-dsd.h ${QMI_SRC}/qmi-enums-fox.h
  ${QMI_SRC}/qmi-enums-ims.h ${QMI_SRC}/qmi-enums-imsp.h ${QMI_SRC}/qmi-enums-imsa.h
  ${QMI_SRC}/qmi-enums-imsdcm.h ${QMI_SRC}/qmi-enums-ssc.h ${QMI_SRC}/qmi-device.h
)

set(QMI_FLAGS64_HEADERS
  ${QMI_SRC}/qmi-flags64-dms.h ${QMI_SRC}/qmi-flags64-nas.h
  ${QMI_SRC}/qmi-flags64-loc.h ${QMI_SRC}/qmi-flags64-dsd.h
  ${QMI_SRC}/qmi-flags64-wds.h
)

# Enum/flag/error types - use pre-generated files
set(QMI_PREGEN ${CMAKE_CURRENT_SOURCE_DIR}/src/libqmi-glib/generated)
file(COPY
  ${QMI_PREGEN}/qmi-enum-types.h ${QMI_PREGEN}/qmi-enum-types.c
  ${QMI_PREGEN}/qmi-flag-types.h ${QMI_PREGEN}/qmi-flag-types.c
  ${QMI_PREGEN}/qmi-error-types.h ${QMI_PREGEN}/qmi-error-types.c
  ${QMI_PREGEN}/qmi-error-quarks.c
  ${QMI_PREGEN}/qmi-flags64-types.h ${QMI_PREGEN}/qmi-flags64-types.c
  ${QMI_PREGEN}/qmi-enum-types-private.h ${QMI_PREGEN}/qmi-enum-types-private.c
  ${QMI_PREGEN}/qmi-flag-types-private.h ${QMI_PREGEN}/qmi-flag-types-private.c
  DESTINATION ${QMI_GEN}
)

# Service codegen - CTL is private
add_custom_command(
  OUTPUT ${QMI_GEN}/qmi-ctl.c ${QMI_GEN}/qmi-ctl.h
  COMMAND ${Python3_EXECUTABLE} ${QMI_CODEGEN}
    --input ${QMI_DATA}/qmi-service-ctl.json
    --include ${QMI_COMMON}
    --output ${QMI_GEN}/qmi-ctl
  DEPENDS ${QMI_DATA}/qmi-service-ctl.json ${QMI_COMMON}
  COMMENT "Generating qmi-ctl"
)

# Public services
set(QMI_SERVICES
  atr dms dpm dsd ims imsa imsp imsdcm fox gas gms loc nas oma pbm pdc pds
  qos sar ssc uim voice wda wds wms
)

set(QMI_GEN_SOURCES ${QMI_GEN}/qmi-ctl.c)
set(QMI_GEN_HEADERS ${QMI_GEN}/qmi-ctl.h)

foreach(_svc ${QMI_SERVICES})
  add_custom_command(
    OUTPUT ${QMI_GEN}/qmi-${_svc}.c ${QMI_GEN}/qmi-${_svc}.h
    COMMAND ${Python3_EXECUTABLE} ${QMI_CODEGEN}
      --input ${QMI_DATA}/qmi-service-${_svc}.json
      --include ${QMI_COMMON}
      --output ${QMI_GEN}/qmi-${_svc}
    DEPENDS ${QMI_DATA}/qmi-service-${_svc}.json ${QMI_COMMON}
    COMMENT "Generating qmi-${_svc}"
  )
  list(APPEND QMI_GEN_SOURCES ${QMI_GEN}/qmi-${_svc}.c)
  list(APPEND QMI_GEN_HEADERS ${QMI_GEN}/qmi-${_svc}.h)
endforeach()

# Core sources
set(QMI_CORE_SOURCES
  ${QMI_SRC}/qmi-device.c ${QMI_SRC}/qmi-message.c ${QMI_SRC}/qmi-client.c
  ${QMI_SRC}/qmi-proxy.c ${QMI_SRC}/qmi-endpoint.c
  ${QMI_SRC}/qmi-endpoint-qmux.c ${QMI_SRC}/qmi-endpoint-mbim.c
  ${QMI_SRC}/qmi-helpers.c ${QMI_SRC}/qmi-utils.c ${QMI_SRC}/qmi-file.c
  ${QMI_SRC}/qmi-net-port-manager.c ${QMI_SRC}/qmi-net-port-manager-qmiwwan.c
  ${QMI_SRC}/qmi-message-context.c
  ${QMI_SRC}/qmi-enums-dms.c ${QMI_SRC}/qmi-enums-nas.c
  ${QMI_SRC}/qmi-enums-wds.c ${QMI_SRC}/qmi-compat.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/common/qmi-common.c
)

# Mkenums generated sources
set(QMI_MKENUMS_SOURCES
  ${QMI_GEN}/qmi-enum-types.c ${QMI_GEN}/qmi-flag-types.c
  ${QMI_GEN}/qmi-error-types.c ${QMI_GEN}/qmi-error-quarks.c
  ${QMI_GEN}/qmi-flags64-types.c
  ${QMI_GEN}/qmi-enum-types-private.c ${QMI_GEN}/qmi-flag-types-private.c
)

add_library(qmi-glib STATIC
  ${QMI_CORE_SOURCES}
  ${QMI_GEN_SOURCES}
  ${QMI_MKENUMS_SOURCES}
)

target_include_directories(qmi-glib
  PUBLIC
    ${QMI_SRC}
    ${QMI_GEN}
    ${DEPS_BUILD}/libqmi  # qmi-version.h
  PRIVATE
    ${QMI_SRC}/kernel
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)

target_compile_definitions(qmi-glib PRIVATE
  LIBQMI_GLIB_COMPILATION
  G_LOG_DOMAIN="Qmi"
  _GNU_SOURCE
  LIBEXEC_PATH="/usr/libexec"
)

target_link_libraries(qmi-glib PUBLIC glib-complete mbim-glib)
