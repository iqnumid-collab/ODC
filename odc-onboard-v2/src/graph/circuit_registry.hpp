#pragma once

#include <DSPatch.h>

#include <memory>
#include <string>
#include <unordered_map>

namespace odc::graph {

class OdcComponent;

class CircuitRegistry {
public:
    CircuitRegistry() = default;

    void add(OdcComponent* component);
    void remove(const std::string& id);
    void clear();
    void tick();

    DSPatch::Circuit& circuit() { return circuit_; }
    const DSPatch::Circuit& circuit() const { return circuit_; }

private:
    DSPatch::Circuit circuit_;
    std::unordered_map<std::string, DSPatch::Component::SPtr> components_;
};

} // namespace odc::graph
