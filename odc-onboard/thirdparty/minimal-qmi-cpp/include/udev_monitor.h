#pragma once
#include <string>
#include <functional>
#include <glib.h>

// Forward declare GUdev types to avoid requiring gudev header in all consumers
typedef struct _GUdevClient GUdevClient;
typedef struct _GUdevDevice GUdevDevice;

namespace uqmi {

struct DeviceInfo {
    std::string device_path;  // e.g. /dev/cdc-wdm0
    std::string iface_name;   // e.g. wwan0
    std::string driver;       // e.g. qmi_wwan
    std::string sysfs_path;
};

// Callback: added=true on plug, false on unplug
using HotplugCb = std::function<void(bool added, const DeviceInfo& info)>;

class UdevMonitor {
public:
    ~UdevMonitor();
    void start(HotplugCb cb);
    void stop();
    void scan_existing();

private:
    GUdevClient* client_ = nullptr;
    gulong       sig_id_ = 0;
    HotplugCb    cb_;
    DeviceInfo   current_;

    void on_uevent(const char* action, GUdevDevice* device);
    bool is_qmi_device(GUdevDevice* device);
    DeviceInfo extract_info(GUdevDevice* device);
    std::string find_net_iface(GUdevDevice* device);
};

} // namespace uqmi
