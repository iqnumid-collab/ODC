#pragma once
#include <libqmi-glib.h>
#include <functional>
#include <string>
#include <cstdint>
#include <vector>

namespace odc {
namespace qmi {

struct IpSettings {
    std::string ipv4_addr;
    std::string gateway;
    std::string dns1;
    std::string dns2;
    uint32_t    mtu    = 0;
    uint32_t    prefix = 32;

    std::string ipv6_addr;
    std::string ipv6_gateway;
    std::string ipv6_dns1;
    std::string ipv6_dns2;
    uint8_t     ipv6_prefix = 64;
};

struct ProfileInfo {
    int         index = -1;
    std::string name;
    std::string apn;
};

using ResultCb = std::function<void(bool success, const char* err)>;
using IpResultCb = std::function<void(bool success, const IpSettings& settings, const char* err)>;
using ProfileIndexCb = std::function<void(bool success, int profile_index, const char* err)>;

struct WdsSession {
    std::string   id;
    QmiClientWds* client = nullptr;
    guint32       pkt_handle = 0;
    std::string   iface;
    bool          owns_client = false; // true if allocated beyond primary wds_
};

class QmiServices {
public:
    ~QmiServices();

    void open_device(const char* path, ResultCb cb);
    void allocate_clients(ResultCb cb);
    void set_modem_online(ResultCb cb);
    void set_operating_mode(QmiDmsOperatingMode mode, ResultCb cb);
    void reset_modem(ResultCb cb);
    void fcc_unlock(const std::string& method, ResultCb cb);

    void get_sim_status(std::function<void(bool ready, bool pin_needed, int pin_retries, const char* err)> cb);
    void verify_pin(const char* pin, ResultCb cb);
    void register_indications(
        std::function<void(bool registered, bool roaming)> nas_cb,
        std::function<void(bool connected)> wds_cb);

    void start_network(const char* apn, int ip_type, const char* auth,
                       const char* user, const char* pass, ResultCb cb);
    void start_network_by_profile(guint8 profile_index, int ip_type, ResultCb cb);
    void get_ip_settings(IpResultCb cb);
    void stop_network(ResultCb cb);

    void create_profile_3gpp(const char* apn, int ip_type, const char* auth,
                             const char* user, const char* pass, ProfileIndexCb cb);
    void modify_profile_3gpp(guint8 profile_index, const char* apn, int ip_type,
                             const char* auth, const char* user, const char* pass, ResultCb cb);
    void delete_profile_3gpp(guint8 profile_index, ResultCb cb);
    void get_profile_settings(guint8 profile_index,
                              std::function<void(bool ok, const ProfileInfo& info, const char* err)> cb);
    void list_profiles_3gpp(std::function<void(bool ok, const std::vector<ProfileInfo>& profiles, const char* err)> cb);
    void set_modem_autoconnect(bool enabled, ResultCb cb);

    /** Allocate an extra WDS CID for a concurrent PDN session. */
    void allocate_extra_wds(const std::string& session_id, const std::string& iface, ResultCb cb);
    void start_session_network(const std::string& session_id,
                               const char* apn, int ip_type, const char* auth,
                               const char* user, const char* pass,
                               int profile_index, ResultCb cb);
    void get_session_ip_settings(const std::string& session_id, IpResultCb cb);
    void stop_session_network(const std::string& session_id, ResultCb cb);
    void release_extra_sessions();

    void close();

    QmiClientDms* dms() { return dms_; }
    QmiClientNas* nas() { return nas_; }
    QmiClientWds* wds() { return wds_; }
    QmiClientUim* uim() { return uim_; }
    QmiClientWda* wda() { return wda_; }

    const std::vector<WdsSession>& sessions() const { return sessions_; }
    WdsSession* find_session(const std::string& id);

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

    std::vector<WdsSession> sessions_;

    void on_client_allocated(QmiDevice* dev, GAsyncResult* res);
    static QmiWdsIpFamily ip_family_from_type(int ip_type);
    static QmiWdsPdpType pdp_type_from_ip(int ip_type);
};

} // namespace qmi
} // namespace odc
