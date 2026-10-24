#pragma once
#include <gudev/gudev.h>
#include <string>
#include <functional>

namespace odc {
namespace qmi {

struct DeviceInfo {
    std::string device_path;
    std::string sysfs_path;
    std::string driver;
    std::string iface_name;
};

using HotplugCb = std::function<void(bool added, const DeviceInfo& info)>;

class QmiWatcher {
public:
    QmiWatcher() = default;
    ~QmiWatcher();

    QmiWatcher(const QmiWatcher&) = delete;
    QmiWatcher& operator=(const QmiWatcher&) = delete;

    void start(HotplugCb cb, GMainContext* ctx = nullptr);
    void stop();

    const DeviceInfo& current() const { return current_; }

private:
    GUdevClient* client_ = nullptr;
    gulong sig_id_ = 0;
    GMainContext* ctx_ = nullptr;
    HotplugCb cb_;
    DeviceInfo current_;

    void on_uevent(const char* action, GUdevDevice* device);
    bool is_qmi_device(GUdevDevice* device);
    std::string find_net_iface(GUdevDevice* device);
    DeviceInfo extract_info(GUdevDevice* device);
    void scan_existing();
};

} // namespace qmi
} // namespace odc
