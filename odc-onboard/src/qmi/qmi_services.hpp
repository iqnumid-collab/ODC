#pragma once
#include <libqmi-glib.h>
#include <functional>
#include <string>
#include <cstdint>

namespace odc {
namespace qmi {

struct IpSettings {
    std::string ipv4_addr;
    std::string gateway;
    std::string dns1;
    std::string dns2;
    uint32_t    mtu    = 0;
    uint32_t    prefix = 32;
};

using ResultCb = std::function<void(bool success, const char* err)>;
using IpResultCb = std::function<void(bool success, const IpSettings& settings, const char* err)>;

class QmiServices {
public:
    ~QmiServices();

    void open_device(const char* path, ResultCb cb);
    void allocate_clients(ResultCb cb);
    void set_modem_online(ResultCb cb);
    void get_sim_status(std::function<void(bool ready, bool pin_needed, const char* err)> cb);
    void verify_pin(const char* pin, ResultCb cb);
    void register_indications(
        std::function<void(bool registered, bool roaming)> nas_cb,
        std::function<void(bool connected)> wds_cb);
    void start_network(const char* apn, int ip_type, const char* auth,
                       const char* user, const char* pass, ResultCb cb);
    void get_ip_settings(IpResultCb cb);
    void stop_network(ResultCb cb);
    void close();

    QmiClientDms* dms() { return dms_; }
    QmiClientNas* nas() { return nas_; }
    QmiClientWds* wds() { return wds_; }
    QmiClientUim* uim() { return uim_; }
    QmiClientWda* wda() { return wda_; }

private:
    QmiDevice*    device_    = nullptr;
    QmiClientDms* dms_       = nullptr;
    QmiClientNas* nas_       = nullptr;
    QmiClientWds* wds_       = nullptr;
    QmiClientUim* uim_       = nullptr;
    QmiClientWda* wda_       = nullptr;
    guint32       pkt_handle_= 0;
    int           clients_pending_ = 0;
    ResultCb      alloc_cb_;

    gulong nas_sig_id_ = 0;
    gulong wds_sig_id_ = 0;

    void on_client_allocated(QmiDevice* dev, GAsyncResult* res);
};

} // namespace qmi
} // namespace odc
