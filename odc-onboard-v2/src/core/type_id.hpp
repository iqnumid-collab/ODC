#pragma once

#include <caf/type_id.hpp>

CAF_BEGIN_TYPE_ID_BLOCK(odc_types, caf::first_custom_type_id)

    CAF_ADD_ATOM(odc_types, odc, mon_tick, "mon_tick")
    CAF_ADD_ATOM(odc_types, odc, mon_start, "mon_start")
    CAF_ADD_ATOM(odc_types, odc, mon_stop, "mon_stop")

CAF_END_TYPE_ID_BLOCK(odc_types)
