#pragma once
#include "config.h"
#include "modem_info.h"
#include "hw_info.h"
#include "states.h"
#include "kafka/producer/producer.h"
#include <thread_orchestrator/thread_orchestrator.hpp>
#include <memory>
#include <mutex>
#include <string>

namespace uqmi {

class EventPublisher {
public:
    EventPublisher(const Config& cfg, orch::ThreadOrchestrator& pool);
    ~EventPublisher();

    void start();
    void stop();

    void publish_state(State s, const ModemHwInfo& hw, const Config& cfg);
    void publish_telemetry(const ModemData& d, const Config& cfg);
    void publish_response(const std::string& correlation_id, bool ok,
                          const std::string& state, const std::string& payload);

private:
    void produce(const std::string& topic, std::string data);

    const Config& cfg_;
    orch::ThreadOrchestrator& pool_;
    std::unique_ptr<kafka::KafkaProducer> producer_;
    std::mutex produce_mu_; // guards producer_->produce()
};

} // namespace uqmi
