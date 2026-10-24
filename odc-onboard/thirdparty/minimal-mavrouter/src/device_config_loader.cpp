/*
 * This file is part of the MAVLink Router project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "device_config_loader.h"

#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sys/stat.h>
#include <time.h>

#include <common/conf_file.h>
#include <common/log.h>

#include "build_config.h"
#include "udev_monitor.h"

std::string DeviceConfigLoader::_config_dir = DEFAULT_DEVICE_CONFIG_DIR;

void DeviceConfigLoader::set_config_dir(const std::string &dir) {
    _config_dir = dir;
}

const std::string &DeviceConfigLoader::get_config_dir() {
    return _config_dir;
}

std::string DeviceConfigLoader::build_filename(const UsbDeviceInfo &info) {
    if (!info.serial.empty() && !info.vendor_id.empty() && !info.model_id.empty()) {
        return info.vendor_id + ":" + info.model_id + "-" + info.serial + ".conf";
    }
    if (!info.vendor_id.empty() && !info.model_id.empty()) {
        return info.vendor_id + ":" + info.model_id + ".conf";
    }
    return "";
}

std::string DeviceConfigLoader::build_fallback_filename(const UsbDeviceInfo &info) {
    if (!info.serial.empty()) {
        return "auto_" + info.serial + ".conf";
    }
    return "";
}

std::string DeviceConfigLoader::find_config(const UsbDeviceInfo &info) {
    if (_config_dir.empty()) return "";

    std::string exact = build_filename(info);
    if (!exact.empty()) {
        std::string path = _config_dir + "/" + exact;
        struct stat st;
        if (stat(path.c_str(), &st) == 0) {
            log_info("Device config found: %s", path.c_str());
            return path;
        }
    }

    if (!info.vendor_id.empty() && !info.model_id.empty()) {
        std::string vp = info.vendor_id + ":" + info.model_id + ".conf";
        std::string path = _config_dir + "/" + vp;
        struct stat st;
        if (stat(path.c_str(), &st) == 0) {
            log_info("Device config found (vendor:product): %s", path.c_str());
            return path;
        }
    }

    std::string default_path = _config_dir + "/default.conf";
    struct stat st;
    if (stat(default_path.c_str(), &st) == 0) {
        log_info("Using default device config: %s", default_path.c_str());
        return default_path;
    }

    return "";
}

std::shared_ptr<UartEndpoint> DeviceConfigLoader::load_and_create(
    const std::string &config_path,
    const UsbDeviceInfo &info)
{
    ConfFile conffile;
    if (conffile.parse(config_path) < 0) {
        log_error("Failed to parse device config: %s", config_path.c_str());
        return nullptr;
    }

    struct ConfFile::section_iter iter = {};
    if (conffile.get_sections("uartendpoint *", &iter) != 0) {
        log_error("No UartEndpoint section found in: %s", config_path.c_str());
        return nullptr;
    }

    UartEndpointConfig conf{};
    conf.name = std::string(iter.name, iter.name_len);
    conf.usb_serial = info.serial;
    conf.usb_vendor = info.vendor_id;
    conf.usb_product = info.model_id;
    conf.device = info.devnode;

    static const ConfFile::OptionsTable option_table[] = {
        {"baud",            false, ConfFile::parse_uint32_vector,   OPTIONS_TABLE_STRUCT_FIELD(UartEndpointConfig, baudrates)},
        {"FlowControl",     false, ConfFile::parse_bool,            OPTIONS_TABLE_STRUCT_FIELD(UartEndpointConfig, flowcontrol)},
        {"AllowMsgIdOut",   false, ConfFile::parse_uint32_vector,   OPTIONS_TABLE_STRUCT_FIELD(UartEndpointConfig, allow_msg_id_out)},
        {"BlockMsgIdOut",   false, ConfFile::parse_uint32_vector,   OPTIONS_TABLE_STRUCT_FIELD(UartEndpointConfig, block_msg_id_out)},
        {"AllowSrcCompOut", false, ConfFile::parse_uint8_vector,    OPTIONS_TABLE_STRUCT_FIELD(UartEndpointConfig, allow_src_comp_out)},
        {"BlockSrcCompOut", false, ConfFile::parse_uint8_vector,    OPTIONS_TABLE_STRUCT_FIELD(UartEndpointConfig, block_src_comp_out)},
        {"AllowSrcSysOut",  false, ConfFile::parse_uint8_vector,    OPTIONS_TABLE_STRUCT_FIELD(UartEndpointConfig, allow_src_sys_out)},
        {"BlockSrcSysOut",  false, ConfFile::parse_uint8_vector,    OPTIONS_TABLE_STRUCT_FIELD(UartEndpointConfig, block_src_sys_out)},
        {"AllowMsgIdIn",    false, ConfFile::parse_uint32_vector,   OPTIONS_TABLE_STRUCT_FIELD(UartEndpointConfig, allow_msg_id_in)},
        {"BlockMsgIdIn",    false, ConfFile::parse_uint32_vector,   OPTIONS_TABLE_STRUCT_FIELD(UartEndpointConfig, block_msg_id_in)},
        {"AllowSrcCompIn",  false, ConfFile::parse_uint8_vector,    OPTIONS_TABLE_STRUCT_FIELD(UartEndpointConfig, allow_src_comp_in)},
        {"BlockSrcCompIn",  false, ConfFile::parse_uint8_vector,    OPTIONS_TABLE_STRUCT_FIELD(UartEndpointConfig, block_src_comp_in)},
        {"AllowSrcSysIn",   false, ConfFile::parse_uint8_vector,    OPTIONS_TABLE_STRUCT_FIELD(UartEndpointConfig, allow_src_sys_in)},
        {"BlockSrcSysIn",   false, ConfFile::parse_uint8_vector,    OPTIONS_TABLE_STRUCT_FIELD(UartEndpointConfig, block_src_sys_in)},
        {"group",           false, ConfFile::parse_stdstring,       OPTIONS_TABLE_STRUCT_FIELD(UartEndpointConfig, group)},
        {}
    };

    if (conffile.extract_options(&iter, option_table, &conf) < 0) {
        log_error("Failed to extract options from: %s", config_path.c_str());
        return nullptr;
    }

    if (conf.baudrates.empty()) {
        conf.baudrates.push_back(115200);
        log_warning("No baudrate specified in %s, defaulting to 115200", config_path.c_str());
    }

    auto endpoint = std::make_shared<UartEndpoint>(conf.name);
    endpoint->set_usb_config(conf.usb_serial, conf.usb_vendor, conf.usb_product);
    endpoint->setup(conf);

    log_info("Created dynamic endpoint '%s' from %s", conf.name.c_str(), config_path.c_str());
    return endpoint;
}

bool DeviceConfigLoader::save_config(const UsbDeviceInfo &info, const UartEndpointConfig &base_config) {
    if (_config_dir.empty()) return false;

    std::string filename = build_filename(info);
    if (filename.empty()) {
        filename = build_fallback_filename(info);
    }
    if (filename.empty()) return false;

    std::string path = _config_dir + "/" + filename;

    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        log_debug("Config already exists: %s", path.c_str());
        return true;
    }

    std::ofstream out(path);
    if (!out.is_open()) {
        log_error("Failed to create device config: %s (%m)", path.c_str());
        return false;
    }

    time_t t = time(nullptr);
    struct tm *tm_info = localtime(&t);
    char timebuf[64];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm_info);

    out << "# Auto-generated on " << timebuf << "\n";
    out << "# Device: " << info.devnode
        << " serial=" << info.serial
        << " vendor=" << info.vendor_id
        << " product=" << info.model_id << "\n";
    out << "[UartEndpoint auto_" << info.serial << "]\n";
    out << "UsbSerial=" << info.serial << "\n";
    if (!info.vendor_id.empty()) out << "UsbVendor=" << info.vendor_id << "\n";
    if (!info.model_id.empty()) out << "UsbProduct=" << info.model_id << "\n";

    for (size_t i = 0; i < base_config.baudrates.size(); i++) {
        out << "Baud=" << base_config.baudrates[i] << "\n";
    }

    if (base_config.flowcontrol) {
        out << "FlowControl=true\n";
    }

    if (!base_config.group.empty()) {
        out << "group=" << base_config.group << "\n";
    }

    out.close();

    log_info("Saved device config: %s", path.c_str());
    return true;
}

std::vector<std::string> DeviceConfigLoader::enumerate_configs() {
    std::vector<std::string> configs;

    DIR *dir = opendir(_config_dir.c_str());
    if (!dir) return configs;

    struct dirent *ent;
    while ((ent = readdir(dir)) != nullptr) {
        std::string name = ent->d_name;
        if (name.size() > 5 && name.substr(name.size() - 5) == ".conf") {
            configs.push_back(_config_dir + "/" + name);
        }
    }

    closedir(dir);
    return configs;
}
