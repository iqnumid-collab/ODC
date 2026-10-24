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
#include "udev_monitor.h"

#include <cstring>
#include <unistd.h>

#include <common/log.h>

UdevMonitor::UdevMonitor() = default;

UdevMonitor::~UdevMonitor() {
    if (_monitor) udev_monitor_unref(_monitor);
    if (_udev) udev_unref(_udev);
    fd = -1;
}

bool UdevMonitor::init() {
    _udev = udev_new();
    if (!_udev) {
        log_error("Failed to create udev context");
        return false;
    }

    _monitor = udev_monitor_new_from_netlink(_udev, "udev");
    if (!_monitor) {
        log_error("Failed to create udev monitor");
        return false;
    }

    if (udev_monitor_filter_add_match_subsystem_devtype(_monitor, "tty", nullptr) < 0) {
        log_error("Failed to add udev filter for tty subsystem");
        return false;
    }

    if (udev_monitor_enable_receiving(_monitor) < 0) {
        log_error("Failed to enable udev monitor receiving");
        return false;
    }

    fd = udev_monitor_get_fd(_monitor);
    if (fd < 0) {
        log_error("Failed to get udev monitor fd");
        return false;
    }

    log_info("Udev monitor initialized on fd %d", fd);
    return true;
}

int UdevMonitor::handle_read() {
    struct udev_device *dev = udev_monitor_receive_device(_monitor);
    if (!dev) return 0;

    const char *action_str = udev_device_get_action(dev);
    UdevAction action = UdevAction::Unknown;
    if (action_str) {
        if (std::strcmp(action_str, "add") == 0) action = UdevAction::Add;
        else if (std::strcmp(action_str, "remove") == 0) action = UdevAction::Remove;
        else if (std::strcmp(action_str, "change") == 0) action = UdevAction::Change;
    }

    UsbDeviceInfo info = parse_device(dev);

    if (_handler) {
        _handler(action, info);
    }

    udev_device_unref(dev);
    return 0;
}

UsbDeviceInfo UdevMonitor::parse_device(struct udev_device *dev) {
    UsbDeviceInfo info;

    const char *devnode = udev_device_get_devnode(dev);
    if (devnode) info.devnode = devnode;

    struct udev_device *usb_dev = udev_device_get_parent_with_subsystem_devtype(dev, "usb", "usb_device");
    if (usb_dev) {
        const char *val;

        val = udev_device_get_sysattr_value(usb_dev, "serial");
        if (val) info.serial = val;

        val = udev_device_get_sysattr_value(usb_dev, "idVendor");
        if (val) info.vendor_id = val;

        val = udev_device_get_sysattr_value(usb_dev, "idProduct");
        if (val) info.model_id = val;

        val = udev_device_get_property_value(usb_dev, "ID_PATH");
        if (val) info.usb_path = val;

        val = udev_device_get_property_value(usb_dev, "ID_VENDOR_FROM_DATABASE");
        if (!val) val = udev_device_get_sysattr_value(usb_dev, "manufacturer");
        if (val) info.manufacturer = val;

        val = udev_device_get_property_value(usb_dev, "ID_MODEL_FROM_DATABASE");
        if (!val) val = udev_device_get_sysattr_value(usb_dev, "product");
        if (val) info.product = val;
    }

    if (info.serial.empty()) {
        const char *val = udev_device_get_property_value(dev, "ID_SERIAL_SHORT");
        if (!val) val = udev_device_get_property_value(dev, "ID_SERIAL");
        if (val) info.serial = val;
    }

    struct udev_list_entry *list_entry;
    udev_list_entry_foreach(list_entry, udev_device_get_devlinks_list_entry(dev)) {
        if (!info.devlinks.empty()) info.devlinks += " ";
        info.devlinks += udev_list_entry_get_name(list_entry);
    }

    log_debug("Udev device: %s serial=%s vendor=%s:%s path=%s",
              info.devnode.c_str(), info.serial.c_str(),
              info.vendor_id.c_str(), info.model_id.c_str(),
              info.usb_path.c_str());

    return info;
}

std::vector<UsbDeviceInfo> UdevMonitor::enumerate_tty_devices() {
    std::vector<UsbDeviceInfo> devices;

    struct udev *udev = udev_new();
    if (!udev) return devices;

    struct udev_enumerate *enumerate = udev_enumerate_new(udev);
    if (enumerate) {
        udev_enumerate_add_match_subsystem(enumerate, "tty");
        udev_enumerate_scan_devices(enumerate);

        struct udev_list_entry *list_entry;
        udev_list_entry_foreach(list_entry, udev_enumerate_get_list_entry(enumerate)) {
            const char *path = udev_list_entry_get_name(list_entry);
            struct udev_device *dev = udev_device_new_from_syspath(udev, path);
            if (dev) {
                devices.push_back(parse_device(dev));
                udev_device_unref(dev);
            }
        }

        udev_enumerate_unref(enumerate);
    }

    udev_unref(udev);
    return devices;
}
