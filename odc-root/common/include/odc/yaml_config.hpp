#pragma once

#include <yaml-cpp/yaml.h>

#include <filesystem>
#include <optional>
#include <string>

namespace odc {

YAML::Node load_yaml(const std::filesystem::path& path);

void save_yaml(const YAML::Node& root, const std::filesystem::path& path);

template<typename T>
std::optional<T> get_value(const YAML::Node& node, const std::string& key) {
    if (!node[key]) {
        return std::nullopt;
    }
    try {
        return node[key].as<T>();
    } catch (const YAML::Exception&) {
        return std::nullopt;
    }
}

bool has_key(const YAML::Node& node, const std::string& key);

} // namespace odc
