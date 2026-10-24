#include "graph/circuit_registry.hpp"

#include "graph/components/odc_component.hpp"

namespace odc::graph {

void CircuitRegistry::add(OdcComponent* component) {
    if (!component) {
        return;
    }

    const auto shared = std::shared_ptr<OdcComponent>(component, [](OdcComponent*) {});
    circuit_.AddComponent(shared);
    components_[component->id()] = shared;
}

void CircuitRegistry::remove(const std::string& id) {
    const auto it = components_.find(id);
    if (it == components_.end()) {
        return;
    }

    circuit_.RemoveComponent(it->second);
    components_.erase(it);
}

void CircuitRegistry::clear() {
    for (const auto& [id, component] : components_) {
        circuit_.RemoveComponent(component);
    }
    components_.clear();
}

void CircuitRegistry::tick() {
    circuit_.Tick();
}

} // namespace odc::graph
