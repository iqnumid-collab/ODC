#include "kafka/event_publisher.h"
#include "log.h"
#include <proto/qmi_agent.pb.h>
#include <chrono>

namespace uqmi {

static int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

EventPublisher::EventPublisher(const Config& cfg, orch::ThreadOrchestrator& pool)
    : cfg_(cfg), pool_(pool) {}

EventPublisher::~EventPublisher() { stop(); }

void EventPublisher::start() {
    try {
        producer_ = std::make_unique<kafka::KafkaProducer>(
            kafka::KafkaProducer::Config{
                .bootstrap_server = cfg_.kafka_broker,
                .client_id        = cfg_.kafka_client_id + "-events"});
        producer_->connect();
        LOG_I("[kafka] event publisher connected to %s", cfg_.kafka_broker.c_str());
    } catch (const std::exception& e) {
        LOG_W("[kafka] event publisher connect failed: %s (events logged to stderr)", e.what());
        producer_.reset();
    }
}

void EventPublisher::stop() {
    producer_.reset();
}

void EventPublisher::publish_state(State s, const ModemHwInfo& hw, const Config& cfg) {
    qmiagent::QmiStateEvent msg;
    msg.set_state(state_name(s));
    msg.set_device_path(cfg.device_path);
    msg.set_iface_name(cfg.iface_name);
    msg.set_timestamp_ms(now_ms());

    if (s == State::CONNECTED && hw.valid()) {
        auto* h = msg.mutable_hw_info();
        h->set_manufacturer(hw.manufacturer);
        h->set_model(hw.model);
        h->set_revision(hw.revision);
        h->set_hw_revision(hw.hw_revision);
        h->set_imei(hw.imei);
        h->set_imsi(hw.imsi);
        h->set_iccid(hw.iccid);
    }

    std::string data;
    msg.SerializeToString(&data);
    produce(cfg_.kafka_events_topic, std::move(data));
}

void EventPublisher::publish_telemetry(const ModemData& d, const Config& cfg) {
    qmiagent::QmiTelemetry msg;
    msg.set_device_path(cfg.device_path);
    msg.set_operating_mode(d.operating_mode);
    msg.set_link_layer_protocol(d.link_layer_protocol);
    msg.set_qos_enabled(d.qos_enabled);
    msg.set_rssi_dbm(d.rssi_dbm);
    msg.set_radio_interface(d.radio_interface);
    msg.set_lte_rsrp(d.lte_rsrp);
    msg.set_lte_snr(d.lte_snr);
    msg.set_registration_state(d.registration_state);
    msg.set_ps_attach_state(d.ps_attach_state);
    msg.set_plmn_mcc(d.plmn_mcc);
    msg.set_plmn_mnc(d.plmn_mnc);
    msg.set_operator_name(d.operator_name);
    msg.set_roaming(d.roaming);
    msg.set_connection_status(d.connection_status);
    msg.set_internet_reachable(d.internet_reachable);
    msg.set_ping_latency_ms(d.ping_latency_ms);
    msg.set_timestamp_ms(now_ms());

    auto* ip = msg.mutable_ip_settings();
    ip->set_ipv4_addr(d.ip_settings.ipv4_addr);
    ip->set_gateway(d.ip_settings.gateway);
    ip->set_dns1(d.ip_settings.dns1);
    ip->set_dns2(d.ip_settings.dns2);
    ip->set_mtu(d.ip_settings.mtu);

    std::string data;
    msg.SerializeToString(&data);
    produce(cfg_.kafka_telemetry_topic, std::move(data));
}

void EventPublisher::publish_response(const std::string& correlation_id, bool ok,
                                      const std::string& state, const std::string& payload) {
    qmiagent::QmiResponse msg;
    msg.set_correlation_id(correlation_id);
    msg.set_ok(ok);
    msg.set_state(state);
    msg.set_payload(payload);
    msg.set_timestamp_ms(now_ms());

    std::string data;
    msg.SerializeToString(&data);
    produce(cfg_.kafka_responses_topic, std::move(data));
}

void EventPublisher::produce(const std::string& topic, std::string data) {
    pool_.submit_simple([this, t = topic, d = std::move(data)]() {
        if (producer_) {
            std::lock_guard lk(produce_mu_);
            try {
                producer_->produce(t, 0, std::nullopt, d);
            } catch (const std::exception& e) {
                LOG_W("[kafka] produce failed: %s", e.what());
            }
        } else {
            LOG_W("[kafka] (no broker) %s (%zu bytes)", t.c_str(), d.size());
        }
    });
}

} // namespace uqmi
