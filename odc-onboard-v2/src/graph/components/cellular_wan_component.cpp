#include "graph/components/cellular_wan_component.hpp"

#include "core/event.hpp"
#include "core/event_bus.hpp"
#include "graph/graph_events.hpp"
#include "qmi/qmi_events.hpp"

namespace odc::graph {

CellularWanComponent::CellularWanComponent(
    EventBus& bus,
    std::string id,
    nlohmann::json config)
    : OdcComponent(std::move(id), "cellular.qmi", std::move(config)),
      bus_(bus),
      qmi_cfg_(parse_qmi_config(config_)) {
    SetOutputCount_(2, {"wan", "metrics"});
}

CellularWanComponent::~CellularWanComponent() {
    stop();
}

void CellularWanComponent::configure(const nlohmann::json& config) {
    OdcComponent::configure(config);
    qmi_cfg_ = parse_qmi_config(config_);
}

void CellularWanComponent::start() {
    if (!enabled_ || state_ == ComponentRuntimeState::Running) {
        return;
    }

    set_state(ComponentRuntimeState::Starting);

    if (!event_sub_) {
        event_sub_ = bus_.subscribe([this](const Event& evt) {
            on_event(evt);
        });
    }

    if (!modem_) {
        modem_ = std::make_unique<qmi::QmiModemController>(qmi_cfg_);
    }

    modem_->start(&bus_);
    if (qmi_cfg_.auto_connect) {
        modem_->request_connect();
    }

    set_state(ComponentRuntimeState::Running);
}

void CellularWanComponent::stop() {
    if (state_ == ComponentRuntimeState::Stopped) {
        return;
    }

    set_state(ComponentRuntimeState::Stopping);

    if (event_sub_) {
        bus_.unsubscribe(event_sub_);
        event_sub_ = 0;
    }

    if (modem_) {
        modem_->stop();
        modem_.reset();
    }

    clear_wan_ready();
    {
        std::lock_guard lock(mutex_);
        metrics_ = CellularMetrics{};
        last_events_.clear();
    }
    set_state(ComponentRuntimeState::Stopped);
}

bool CellularWanComponent::request_connect() {
    if (!modem_) {
        return false;
    }
    return modem_->request_connect();
}

bool CellularWanComponent::request_hard_reset() {
    if (!modem_) {
        return false;
    }
    return modem_->request_hard_reset();
}

void CellularWanComponent::list_profiles(
    std::function<void(bool ok, nlohmann::json profiles, std::string error)> cb) {
    if (!modem_) {
        cb(false, nlohmann::json::array(), "modem not running");
        return;
    }
    modem_->list_profiles([cb = std::move(cb)](bool ok, const std::vector<qmi::ProfileInfo>& profiles, const char* err) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& p : profiles) {
            arr.push_back({{"index", p.index}, {"name", p.name}, {"apn", p.apn}});
        }
        cb(ok, std::move(arr), err ? err : "");
    });
}

nlohmann::json CellularWanComponent::status_json() const {
    nlohmann::json status{
        {"id", id_},
        {"type", type_},
        {"enabled", enabled_},
        {"state", std::string(to_string_view(state_))},
        {"device_path", qmi_cfg_.device_path},
        {"iface_name", qmi_cfg_.iface_name},
        {"auto_connect", qmi_cfg_.auto_connect},
        {"allow_roaming", qmi_cfg_.allow_roaming},
        {"profile_index", qmi_cfg_.profile_index},
        {"ensure_profile", qmi_cfg_.ensure_profile},
        {"modem_autoconnect", qmi_cfg_.modem_autoconnect},
        {"hard_reset_mode", qmi_cfg_.hard_reset_mode},
        {"hard_reset_after_soft_fails", qmi_cfg_.hard_reset_after_soft_fails},
        {"fcc_unlock", {
            {"enabled", qmi_cfg_.fcc_unlock.enabled},
            {"method", qmi_cfg_.fcc_unlock.method},
        }},
    };

    if (!qmi_cfg_.pdns.empty()) {
        nlohmann::json pdns = nlohmann::json::array();
        for (const auto& p : qmi_cfg_.pdns) {
            pdns.push_back({
                {"id", p.id},
                {"apn", p.apn},
                {"ip_type", p.ip_type},
                {"iface", p.iface},
                {"profile_index", p.profile_index},
            });
        }
        status["pdns"] = pdns;
    }

    if (modem_) {
        status["qmi_state"] = qmi::to_string_view(modem_->sm_state());
        status["pin_retries_left"] = modem_->pin_retries_left();
        status["soft_fail_count"] = modem_->soft_fail_count();
        status["active_profile_index"] = modem_->active_profile_index();
        if (!modem_->last_reset_stage().empty()) {
            status["reset_stage"] = modem_->last_reset_stage();
        }
        nlohmann::json pdns = nlohmann::json::array();
        for (const auto& p : modem_->pdn_status()) {
            pdns.push_back({
                {"id", p.id},
                {"iface", p.iface},
                {"ipv4_address", p.ipv4_address},
                {"ipv6_address", p.ipv6_address},
                {"connected", p.connected},
            });
        }
        status["pdn_sessions"] = pdns;
    }

    std::lock_guard lock(mutex_);
    status["wan_ready"] = wan_ready_.ready;
    if (wan_ready_.ready) {
        status["ipv4_address"] = wan_ready_.ipv4_address;
        status["ipv6_address"] = wan_ready_.ipv6_address;
        status["wan_iface"] = wan_ready_.iface_name;
    }
    status["last_events"] = last_events_;
    if (metrics_.valid) {
        status["metrics"] = {
            {"rssi_dbm", metrics_.rssi_dbm},
            {"lte_rsrp", metrics_.lte_rsrp},
            {"lte_snr", metrics_.lte_snr},
            {"registration_state", metrics_.registration_state},
            {"roaming", metrics_.roaming},
            {"connection_status", metrics_.connection_status},
            {"internet_reachable", metrics_.internet_reachable},
            {"ping_latency_ms", metrics_.ping_latency_ms},
            {"operator_name", metrics_.operator_name},
            {"iface_name", metrics_.iface_name},
        };
    }

    return status;
}

void CellularWanComponent::set_wan_ready_callback(WanReadyCallback cb) {
    std::lock_guard lock(mutex_);
    wan_callback_ = std::move(cb);
    if (wan_callback_ && wan_ready_.ready) {
        wan_callback_(wan_ready_);
    }
}

std::optional<WanReady> CellularWanComponent::get_wan_ready() const {
    std::lock_guard lock(mutex_);
    if (!wan_ready_.ready) {
        return std::nullopt;
    }
    return wan_ready_;
}

qmi::QmiConfig CellularWanComponent::parse_qmi_config(const nlohmann::json& config) {
    qmi::QmiConfig cfg;
    cfg.device_path = config.value("device_path", config.value("device", ""));
    cfg.iface_name = config.value("iface_name", config.value("iface", "wwan0"));
    cfg.apn = config.value("apn", "");
    cfg.auto_connect = config.value("auto_connect", true);
    cfg.allow_roaming = config.value("allow_roaming", true);
    cfg.username = config.value("username", "");
    cfg.password = config.value("password", "");
    cfg.pin = config.value("pin", "");
    cfg.auth_type = config.value("auth_type", "none");
    cfg.ip_type = config.value("ip_type", 4);
    cfg.profile_index = config.value("profile_index", -1);
    cfg.ensure_profile = config.value("ensure_profile", false);
    cfg.modem_autoconnect = config.value("modem_autoconnect", false);
    cfg.max_retries = config.value("max_retries", cfg.max_retries);
    cfg.backoff_base_ms = config.value("backoff_base_ms", cfg.backoff_base_ms);
    cfg.backoff_max_ms = config.value("backoff_max_ms", cfg.backoff_max_ms);
    cfg.backoff_jitter_ms = config.value("backoff_jitter_ms", cfg.backoff_jitter_ms);
    cfg.watchdog_interval_s = config.value("watchdog_interval_s", cfg.watchdog_interval_s);
    cfg.ping_target = config.value("ping_target", cfg.ping_target);
    cfg.modem_info_interval_s = config.value("modem_info_interval_s", cfg.modem_info_interval_s);
    cfg.open_timeout_ms = config.value("open_timeout_ms", cfg.open_timeout_ms);
    cfg.init_timeout_ms = config.value("init_timeout_ms", cfg.init_timeout_ms);
    cfg.sim_timeout_ms = config.value("sim_timeout_ms", cfg.sim_timeout_ms);
    cfg.reg_timeout_ms = config.value("reg_timeout_ms", cfg.reg_timeout_ms);
    cfg.data_timeout_ms = config.value("data_timeout_ms", cfg.data_timeout_ms);
    cfg.hard_reset_after_soft_fails = config.value("hard_reset_after_soft_fails", cfg.hard_reset_after_soft_fails);
    cfg.hard_reset_mode = config.value("hard_reset_mode", cfg.hard_reset_mode);
    cfg.hard_reset_settle_ms = config.value("hard_reset_settle_ms", cfg.hard_reset_settle_ms);

    if (config.contains("fcc_unlock") && config["fcc_unlock"].is_object()) {
        const auto& fcc = config["fcc_unlock"];
        cfg.fcc_unlock.enabled = fcc.value("enabled", false);
        cfg.fcc_unlock.method = fcc.value("method", "standard");
    }

    if (config.contains("pdns") && config["pdns"].is_array()) {
        for (const auto& entry : config["pdns"]) {
            qmi::PdnConfig pdn;
            pdn.id = entry.value("id", "");
            pdn.apn = entry.value("apn", "");
            pdn.ip_type = entry.value("ip_type", 4);
            pdn.iface = entry.value("iface", entry.value("iface_name", ""));
            pdn.profile_index = entry.value("profile_index", -1);
            pdn.auth_type = entry.value("auth_type", "none");
            pdn.username = entry.value("username", "");
            pdn.password = entry.value("password", "");
            if (!pdn.id.empty()) {
                cfg.pdns.push_back(std::move(pdn));
            }
        }
    }
    return cfg;
}

void CellularWanComponent::on_event(const Event& evt) {
    if (const auto* conn_evt = std::get_if<qmi::QmiConnectionEvent>(&evt)) {
        if (!qmi_cfg_.device_path.empty() && conn_evt->device_path != qmi_cfg_.device_path) {
            return;
        }

        // Primary WAN ready only for primary / empty pdn_id
        if (!conn_evt->pdn_id.empty() && conn_evt->pdn_id != "internet") {
            if (!qmi_cfg_.pdns.empty() && conn_evt->pdn_id != qmi_cfg_.pdns.front().id) {
                return;
            }
        }

        WanReady wan;
        wan.component_id = id_;
        wan.iface_name = conn_evt->iface_name;
        wan.ipv4_address = conn_evt->ipv4_address;
        wan.ipv6_address = conn_evt->ipv6_address;
        wan.ready = !conn_evt->ipv4_address.empty() || !conn_evt->ipv6_address.empty();
        update_wan_ready(wan);
        return;
    }

    if (const auto* telem = std::get_if<qmi::QmiTelemetryEvent>(&evt)) {
        if (!qmi_cfg_.device_path.empty() && telem->device_path != qmi_cfg_.device_path) {
            return;
        }
        CellularMetrics m;
        m.component_id = id_;
        m.iface_name = qmi_cfg_.iface_name;
        m.valid = true;
        m.rssi_dbm = telem->rssi_dbm;
        m.lte_rsrp = telem->lte_rsrp;
        m.lte_snr = telem->lte_snr;
        m.registration_state = telem->registration_state;
        m.roaming = telem->roaming;
        m.connection_status = telem->connection_status;
        m.internet_reachable = telem->internet_reachable;
        m.ping_latency_ms = telem->ping_latency_ms;
        m.operator_name = telem->operator_name;
        update_metrics(m);
        return;
    }

    if (const auto* state_evt = std::get_if<qmi::QmiStateChangeEvent>(&evt)) {
        if (!qmi_cfg_.device_path.empty() && state_evt->device_path != qmi_cfg_.device_path) {
            return;
        }

        if (state_evt->new_state != qmi::QmiState::Connected) {
            clear_wan_ready();
        }
    }
}

void CellularWanComponent::update_wan_ready(const WanReady& wan) {
    WanReadyCallback callback;
    bool became_ready = false;
    {
        std::lock_guard lock(mutex_);
        became_ready = wan.ready && !wan_ready_.ready;
        wan_ready_ = wan;
        callback = wan_callback_;
        if (became_ready) {
            last_events_.push_back(kWanReadyEvent);
            if (last_events_.size() > 16) {
                last_events_.erase(last_events_.begin());
            }
        }
    }

    if (became_ready) {
        GraphTriggerFiredEvent fired;
        fired.trigger_id = id_ + ".wan_ready";
        fired.on_event = kWanReadyEvent;
        fired.source_component = id_;
        bus_.publish(fired);
    }

    if (callback) {
        callback(wan);
    }
}

void CellularWanComponent::clear_wan_ready() {
    WanReady cleared;
    cleared.component_id = id_;
    cleared.ready = false;
    update_wan_ready(cleared);
}

void CellularWanComponent::update_metrics(const CellularMetrics& metrics) {
    std::lock_guard lock(mutex_);
    metrics_ = metrics;
}

void CellularWanComponent::Process_(DSPatch::SignalBus&, DSPatch::SignalBus& outputs) {
    std::lock_guard lock(mutex_);

    if (wan_ready_.ready) {
        outputs.SetValue(kOutputWan, wan_ready_);
    } else {
        outputs.ClearValue(kOutputWan);
    }

    if (metrics_.valid) {
        outputs.SetValue(kOutputMetrics, metrics_);
    } else {
        outputs.ClearValue(kOutputMetrics);
    }
}

} // namespace odc::graph
