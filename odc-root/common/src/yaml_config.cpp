#include "odc/yaml_config.hpp"

#include <fstream>
#include <iostream>

namespace odc {

YAML::Node load_yaml(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return YAML::Node();
    }
    try {
        return YAML::LoadFile(path.string());
    } catch (const YAML::Exception&) {
        return YAML::Node();
    }
}

void save_yaml(const YAML::Node& root, const std::filesystem::path& path) {
    std::filesystem::path parent = path.parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent)) {
        std::filesystem::create_directories(parent);
    }

    std::filesystem::path tmp_path = path.string() + ".tmp";

    {
        YAML::Emitter emitter;
        emitter.SetIndent(2);
        emitter.SetMapFormat(YAML::Block);
        emitter << root;

        std::ofstream ofs(tmp_path);
        if (!ofs) {
            return;
        }
        ofs << emitter.c_str();
        ofs.close();
    }

    std::filesystem::path bak_path = path.string() + ".bak";

    if (std::filesystem::exists(path)) {
        std::error_code ec;
        std::filesystem::rename(path, bak_path, ec);
    }

    std::error_code ec;
    std::filesystem::rename(tmp_path, path, ec);

    if (!ec) {
        if (std::filesystem::exists(bak_path) && std::filesystem::exists(path)) {
            std::filesystem::remove(bak_path);
        }
    }
}

bool has_key(const YAML::Node& node, const std::string& key) {
    return node[key].IsDefined() && !node[key].IsNull();
}

} // namespace odc
