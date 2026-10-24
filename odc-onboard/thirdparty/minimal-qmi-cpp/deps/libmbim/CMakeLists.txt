# libmbim-glib static library with codegen

set(MBIM_MAJOR_VERSION 1)
set(MBIM_MINOR_VERSION 34)
set(MBIM_MICRO_VERSION 0)

# Generate mbim-version.h
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/src/libmbim-glib/mbim-version.h.in
  ${DEPS_BUILD}/libmbim/mbim-version.h
  @ONLY
)

set(MBIM_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/libmbim-glib)
set(MBIM_GEN ${DEPS_BUILD}/libmbim/generated)
set(MBIM_DATA ${CMAKE_CURRENT_SOURCE_DIR}/data)
set(MBIM_CODEGEN ${CMAKE_CURRENT_SOURCE_DIR}/build-aux/mbim-codegen/mbim-codegen)
set(MBIM_MKENUMS ${CMAKE_CURRENT_SOURCE_DIR}/build-aux/mbim-mkenums)
set(MBIM_TEMPLATES ${CMAKE_CURRENT_SOURCE_DIR}/build-aux/templates)

file(MAKE_DIRECTORY ${MBIM_GEN})

# Enum/flag/error types - use pre-generated files from meson build
set(MBIM_PREGEN ${CMAKE_CURRENT_SOURCE_DIR}/src/libmbim-glib/generated)
file(COPY ${MBIM_PREGEN}/mbim-enum-types.h ${MBIM_PREGEN}/mbim-flag-types.h
     ${MBIM_PREGEN}/mbim-error-types.h DESTINATION ${MBIM_GEN})
file(COPY ${MBIM_PREGEN}/mbim-enum-types.c ${MBIM_PREGEN}/mbim-flag-types.c
     ${MBIM_PREGEN}/mbim-error-types.c ${MBIM_PREGEN}/mbim-error-quarks.c
     DESTINATION ${MBIM_GEN})

# Service codegen
set(MBIM_GEN_SOURCES "")
set(MBIM_GEN_HEADERS "")

# Helper function for service codegen
function(mbim_service_codegen _service)
  set(_json_paths "")
  foreach(_j ${ARGN})
    list(APPEND _json_paths "${MBIM_DATA}/${_j}")
  endforeach()
  set(_out_c ${MBIM_GEN}/mbim-${_service}.c)
  set(_out_h ${MBIM_GEN}/mbim-${_service}.h)
  add_custom_command(
    OUTPUT ${_out_c} ${_out_h}
    COMMAND ${Python3_EXECUTABLE} ${MBIM_CODEGEN}
      --output ${MBIM_GEN}/mbim-${_service}
      ${_json_paths}
    DEPENDS ${_json_paths} ${MBIM_CODEGEN}
    COMMENT "Generating mbim-${_service}"
  )
  set(MBIM_GEN_SOURCES ${MBIM_GEN_SOURCES} ${_out_c} PARENT_SCOPE)
  set(MBIM_GEN_HEADERS ${MBIM_GEN_HEADERS} ${_out_h} PARENT_SCOPE)
endfunction()

mbim_service_codegen(atds mbim-service-atds.json)
mbim_service_codegen(auth mbim-service-auth.json)
mbim_service_codegen(basic-connect mbim-service-basic-connect.json mbim-service-ms-basic-connect-v2.json mbim-service-ms-basic-connect-v3.json)
mbim_service_codegen(dss mbim-service-dss.json)
mbim_service_codegen(intel-firmware-update mbim-service-intel-firmware-update.json mbim-service-intel-firmware-update-v2.json)
mbim_service_codegen(intel-thermal-rf mbim-service-intel-thermal-rf.json)
mbim_service_codegen(ms-basic-connect-extensions mbim-service-ms-basic-connect-extensions.json mbim-service-ms-basic-connect-extensions-v2.json mbim-service-ms-basic-connect-extensions-v3.json)
mbim_service_codegen(ms-uicc-low-level-access mbim-service-ms-uicc-low-level-access.json)
mbim_service_codegen(ms-firmware-id mbim-service-ms-firmware-id.json)
mbim_service_codegen(ms-host-shutdown mbim-service-ms-host-shutdown.json)
mbim_service_codegen(ms-sar mbim-service-ms-sar.json)
mbim_service_codegen(ms-voice-extensions mbim-service-ms-voice-extensions.json)
mbim_service_codegen(phonebook mbim-service-phonebook.json)
mbim_service_codegen(proxy-control mbim-service-proxy-control.json)
mbim_service_codegen(qdu mbim-service-qdu.json)
mbim_service_codegen(quectel mbim-service-quectel.json)
mbim_service_codegen(qmi mbim-service-qmi.json)
mbim_service_codegen(sms mbim-service-sms.json)
mbim_service_codegen(stk mbim-service-stk.json)
mbim_service_codegen(ussd mbim-service-ussd.json)
mbim_service_codegen(intel-mutual-authentication mbim-service-intel-mutual-authentication.json)
mbim_service_codegen(intel-tools mbim-service-intel-tools.json)
mbim_service_codegen(google mbim-service-google.json)
mbim_service_codegen(fibocom mbim-service-fibocom.json)
mbim_service_codegen(compal mbim-service-compal.json)
mbim_service_codegen(intel-at-tunnel mbim-service-intel-at-tunnel.json)

# Core sources
set(MBIM_CORE_SOURCES
  ${MBIM_SRC}/mbim-device.c ${MBIM_SRC}/mbim-message.c ${MBIM_SRC}/mbim-proxy.c
  ${MBIM_SRC}/mbim-uuid.c ${MBIM_SRC}/mbim-cid.c ${MBIM_SRC}/mbim-utils.c
  ${MBIM_SRC}/mbim-helpers.c ${MBIM_SRC}/mbim-helpers-netlink.c
  ${MBIM_SRC}/mbim-net-port-manager.c ${MBIM_SRC}/mbim-net-port-manager-wdm.c
  ${MBIM_SRC}/mbim-net-port-manager-wwan.c ${MBIM_SRC}/mbim-proxy-helpers.c
  ${MBIM_SRC}/mbim-compat.c ${MBIM_SRC}/mbim-tlv.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/common/mbim-common.c
)

add_library(mbim-glib STATIC
  ${MBIM_CORE_SOURCES}
  ${MBIM_GEN_SOURCES}
  ${MBIM_GEN}/mbim-enum-types.c
  ${MBIM_GEN}/mbim-flag-types.c
  ${MBIM_GEN}/mbim-error-types.c
  ${MBIM_GEN}/mbim-error-quarks.c
)

target_include_directories(mbim-glib
  PUBLIC
    ${MBIM_SRC}
    ${MBIM_GEN}
    ${DEPS_BUILD}/libmbim  # mbim-version.h
  PRIVATE
    ${MBIM_SRC}/kernel
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)

target_compile_definitions(mbim-glib PRIVATE
  LIBMBIM_GLIB_COMPILATION
  G_LOG_DOMAIN="Mbim"
  _GNU_SOURCE
  LIBEXEC_PATH="/usr/libexec"
)

target_link_libraries(mbim-glib PUBLIC glib-complete)
