#include "graph/persistence_manager.hpp"

namespace odc::graph {

PersistenceManager::PersistenceManager(std::filesystem::path path)
    : path_(std::move(path)) {}

std::optional<GraphSchema> PersistenceManager::load() const {
    return load_graph_schema(path_);
}

bool PersistenceManager::save(const GraphSchema& schema) const {
    return save_graph_schema(path_, schema);
}

} // namespace odc::graph
