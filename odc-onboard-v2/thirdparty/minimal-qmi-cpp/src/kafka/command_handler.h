#pragma once
#include "config.h"
#include "states.h"
#include "kafka/event_publisher.h"
#include "kafka/consumer/consumer.h"
#include <thread_orchestrator/thread_orchestrator.hpp>
#include <memory>
#include <string>

namespace uqmi {

class Controller; // forward declaration

class CommandHandler {
public:
    CommandHandler(const Config& cfg, Controller& ctrl, EventPublisher& pub);
    ~CommandHandler();

    void start();
    void stop();

private:
    bool tryConnect();
    void pollLoop(orch::CancellationToken tok);
    void dispatch(const std::string& data);

    const Config&    cfg_;
    Controller&      ctrl_;
    EventPublisher&  pub_;
    std::unique_ptr<kafka::KafkaConsumer> consumer_;
    orch::OrchestratorFuture<void> future_{};
};

} // namespace uqmi
