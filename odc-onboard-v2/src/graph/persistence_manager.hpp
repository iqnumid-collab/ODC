#pragma once

#include "graph/graph_schema.hpp"

#include <filesystem>
#include <optional>

namespace odc::graph {

class PersistenceManager {
public:
    explicit PersistenceManager(std::filesystem::path path);

    std::optional<GraphSchema> load() const;
    bool save(const GraphSchema& schema) const;

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

} // namespace odc::graph
