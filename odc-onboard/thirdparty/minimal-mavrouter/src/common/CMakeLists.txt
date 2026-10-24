add_library(common STATIC
    conf_file.cpp
    log.cpp
    util.cpp
    xtermios.cpp
)

target_include_directories(common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/modules/mavlink_c_library_v2/ardupilotmega
)

set_target_properties(common PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
