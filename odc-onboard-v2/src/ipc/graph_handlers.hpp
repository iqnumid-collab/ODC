#pragma once

#include "ipc/method_registry.hpp"

namespace odc::graph {

class GraphRuntime;

void register_graph_methods(odc::ipc::MethodRegistry& registry, GraphRuntime& runtime);

} // namespace odc::graph
