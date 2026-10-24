#include "udev_monitor.h"
#include "log.h"
#include <gudev/gudev.h>
#include <cstring>

namespace uqmi {

UdevMonitor::~UdevMonitor() { stop(); }

void UdevMonitor::stop() {
    if (sig_id_ && client_) { g_signal_handler_disconnect(client_, sig_id_); sig_id_ = 0; }
    if (client_) { g_object_unref(client_); client_ = nullptr; }
}

void UdevMonitor::start(HotplugCb cb) {
    stop();
    cb_ = std::move(cb);

    const gchar* subsystems[] = {"usbmisc", "net", nullptr};
    client_ = g_udev_client_new(subsystems);

    sig_id_ = g_signal_connect(client_, "uevent",
        G_CALLBACK(+[](GUdevClient*, const gchar* action, GUdevDevice* dev, gpointer data) {
            static_cast<UdevMonitor*>(data)->on_uevent(action, dev);
        }), this);

    LOG_I("udev monitor started (usbmisc, net)");

    // Defer initial scan to an idle callback so GMainLoop is running
    // when the first DEVICE_DETECTED fires (needed for async QMI ops)
    g_idle_add([](gpointer data) -> gboolean {
        static_cast<UdevMonitor*>(data)->scan_existing();
        return G_SOURCE_REMOVE;
    }, this);
}

bool UdevMonitor::is_qmi_device(GUdevDevice* device) {
    const gchar* subsys = g_udev_device_get_subsystem(device);
    if (!subsys) return false;

    if (std::strcmp(subsys, "usbmisc") == 0) {
        const gchar* name = g_udev_device_get_name(device);
        if (!name || std::strncmp(name, "cdc-wdm", 7) != 0) return false;

        // Check parent USB interface for qmi_wwan driver
        GUdevDevice* parent = g_udev_device_get_parent(device);
        if (parent) {
            const gchar* drv = g_udev_device_get_driver(parent);
            if (drv && (std::strcmp(drv, "qmi_wwan") == 0 || std::strcmp(drv, "cdc_wdm") == 0)) {
                g_object_unref(parent);
                return true;
            }
            g_object_unref(parent);
        }
        // Fallback: any cdc-wdm device is a candidate
        return true;
    }
    return false;
}

std::string UdevMonitor::find_net_iface(GUdevDevice* device) {
    // Walk up to the USB device, then look for net children
    GUdevDevice* usb_dev = g_udev_device_get_parent_with_subsystem(device, "usb", "usb_device");
    if (!usb_dev) return "wwan0"; // fallback

    // Query net subsystem for devices under the same USB parent
    GList* net_devs = g_udev_client_query_by_subsystem(client_, "net");
    std::string result = "wwan0";
    const gchar* usb_path = g_udev_device_get_sysfs_path(usb_dev);

    for (GList* l = net_devs; l; l = l->next) {
        auto* nd = G_UDEV_DEVICE(l->data);
        const gchar* name = g_udev_device_get_name(nd);
        if (!name || std::strncmp(name, "wwan", 4) != 0) continue;

        // Check if this net device shares the same USB parent
        GUdevDevice* np = g_udev_device_get_parent_with_subsystem(nd, "usb", "usb_device");
        if (np) {
            if (usb_path && std::strcmp(g_udev_device_get_sysfs_path(np), usb_path) == 0)
                result = name;
            g_object_unref(np);
            if (result != "wwan0") break;
        }
    }
    g_list_free_full(net_devs, g_object_unref);
    g_object_unref(usb_dev);
    return result;
}

DeviceInfo UdevMonitor::extract_info(GUdevDevice* device) {
    DeviceInfo info;
    const gchar* devfile = g_udev_device_get_device_file(device);
    info.device_path = devfile ? devfile : "";
    info.sysfs_path = g_udev_device_get_sysfs_path(device) ?: "";

    GUdevDevice* parent = g_udev_device_get_parent(device);
    if (parent) {
        const gchar* drv = g_udev_device_get_driver(parent);
        info.driver = drv ? drv : "";
        g_object_unref(parent);
    }

    info.iface_name = find_net_iface(device);
    return info;
}

void UdevMonitor::on_uevent(const char* action, GUdevDevice* device) {
    const gchar* subsys = g_udev_device_get_subsystem(device);

    if (std::strcmp(action, "add") == 0 || std::strcmp(action, "bind") == 0) {
        if (subsys && std::strcmp(subsys, "usbmisc") == 0 && is_qmi_device(device)) {
            auto info = extract_info(device);
            if (info.device_path.empty()) return;
            LOG_I("udev: QMI device detected: %s (driver: %s, iface: %s)",
                  info.device_path.c_str(), info.driver.c_str(), info.iface_name.c_str());
            current_ = info;
            if (cb_) cb_(true, current_);
        }
    } else if (std::strcmp(action, "remove") == 0 || std::strcmp(action, "unbind") == 0) {
        if (subsys && std::strcmp(subsys, "usbmisc") == 0) {
            const gchar* name = g_udev_device_get_name(device);
            if (!name || std::strncmp(name, "cdc-wdm", 7) != 0) return;
            // Check if this is our tracked device
            std::string removed_path = std::string("/dev/") + name;
            if (!current_.device_path.empty() &&
                (current_.device_path == removed_path || current_.sysfs_path == (g_udev_device_get_sysfs_path(device) ?: ""))) {
                LOG_I("udev: QMI device removed: %s", current_.device_path.c_str());
                if (cb_) cb_(false, current_);
                current_ = {};
            }
        }
    }
}

void UdevMonitor::scan_existing() {
    GList* devices = g_udev_client_query_by_subsystem(client_, "usbmisc");
    for (GList* l = devices; l; l = l->next) {
        auto* dev = G_UDEV_DEVICE(l->data);
        if (is_qmi_device(dev)) {
            auto info = extract_info(dev);
            if (!info.device_path.empty()) {
                LOG_I("udev: existing QMI device found: %s (driver: %s, iface: %s)",
                      info.device_path.c_str(), info.driver.c_str(), info.iface_name.c_str());
                current_ = info;
                if (cb_) cb_(true, current_);
                break; // use first found
            }
        }
    }
    g_list_free_full(devices, g_object_unref);

    if (current_.device_path.empty())
        LOG_I("udev: no QMI device found, waiting for hotplug...");
}

} // namespace uqmi
