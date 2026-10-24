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

#include <libudev.h>
#include <string>
#include <vector>
#include <functional>

#include "pollable.h"

struct UsbDeviceInfo {
    std::string devnode;
    std::string serial;
    std::string vendor_id;
    std::string model_id;
    std::string usb_path;
    std::string manufacturer;
    std::string product;
    std::string devlinks;
};

enum class UdevAction { Add, Remove, Change, Unknown };

class UdevMonitor : public Pollable {
public:
    UdevMonitor();
    ~UdevMonitor() override;

    bool init();

    int handle_read() override;
    bool handle_canwrite() override { return false; }
    bool is_critical() override { return false; }
    std::string get_display_name() const override { return "udev-monitor"; }

    using EventHandler = std::function<void(UdevAction, const UsbDeviceInfo &)>;
    void set_event_handler(EventHandler handler) { _handler = std::move(handler); }

    static std::vector<UsbDeviceInfo> enumerate_tty_devices();

private:
    struct udev *_udev = nullptr;
    struct udev_monitor *_monitor = nullptr;
    EventHandler _handler;

    static UsbDeviceInfo parse_device(struct udev_device *dev);
};
