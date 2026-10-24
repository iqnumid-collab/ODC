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
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "endpoint.h"

struct UsbDeviceInfo;

class DeviceConfigLoader {
public:
    static void set_config_dir(const std::string &dir);
    static const std::string &get_config_dir();

    static std::string find_config(const UsbDeviceInfo &info);

    static std::shared_ptr<UartEndpoint> load_and_create(
        const std::string &config_path,
        const UsbDeviceInfo &info);

    static bool save_config(const UsbDeviceInfo &info, const UartEndpointConfig &base_config);

    static std::vector<std::string> enumerate_configs();

private:
    static std::string _config_dir;

    static std::string build_filename(const UsbDeviceInfo &info);
    static std::string build_fallback_filename(const UsbDeviceInfo &info);
};
