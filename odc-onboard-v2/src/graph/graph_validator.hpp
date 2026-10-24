#pragma once

#include "graph/graph_schema.hpp"

#include <string>
#include <vector>

namespace odc::graph {

struct ValidationError {
    std::string message;
};

class GraphValidator {
public:
    std::vector<ValidationError> validate(const GraphSchema& schema) const;
    bool is_valid(const GraphSchema& schema) const;
};

} // namespace odc::graph
