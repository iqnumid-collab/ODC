#include "kafka/command_handler.h"
#include "controller.h"
#include "log.h"
#include <proto/qmi_agent.pb.h>
#include <glib.h>
#include <chrono>
#include <thread>

namespace uqmi {

CommandHandler::CommandHandler(const Config& cfg, Controller& ctrl, EventPublisher& pub)
    : cfg_(cfg), ctrl_(ctrl), pub_(pub) {}

CommandHandler::~CommandHandler() { stop(); }

void CommandHandler::start() {
    future_ = ctrl_.pool().submit([this](orch::CancellationToken tok) {
        pollLoop(std::move(tok));
    });
}

void CommandHandler::stop() {
    if (future_.is_ready()) return;
    future_.cancel();
    try { future_.get(); } catch (...) {}
}

bool CommandHandler::tryConnect() {
    try {
        kafka::KafkaProducer bootstrap({
            .bootstrap_server = cfg_.kafka_broker,
            .client_id        = cfg_.kafka_client_id + "-cmd-bootstrap"});
        bootstrap.connect();
        bootstrap.produce(cfg_.kafka_commands_topic, 0, std::nullopt, "");

        consumer_ = std::make_unique<kafka::KafkaConsumer>(kafka::KafkaConsumer::Config{
            .bootstrap_server = cfg_.kafka_broker,
            .client_id        = cfg_.kafka_client_id + "-commands"});
        consumer_->connect();
        consumer_->assign(cfg_.kafka_commands_topic, 0, -1);
        LOG_I("[kafka] command handler listening on %s", cfg_.kafka_commands_topic.c_str());
        return true;
    } catch (const std::exception& e) {
        LOG_W("[kafka] command handler connect failed: %s (retrying...)", e.what());
        consumer_.reset();
        return false;
    }
}

void CommandHandler::pollLoop(orch::CancellationToken tok) {
    while (!tok.is_cancelled() && !tryConnect())
        std::this_thread::sleep_for(std::chrono::seconds(2));

    while (!tok.is_cancelled()) {
        if (!consumer_) {
            if (!tryConnect())
                std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }
        try {
            auto records = consumer_->poll();
            if (records.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }
            for (auto& rec : records) {
                if (rec.value && !rec.value->empty())
                    dispatch(*rec.value);
            }
        } catch (const std::exception& e) {
            LOG_W("[kafka] command poll error: %s", e.what());
            consumer_.reset();
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

// Data passed through g_idle_add for thread-safe dispatch to GLib main loop
struct IdleDispatch {
    Controller*     ctrl;
    EventPublisher* pub;
    Event           event;
    std::string     correlation_id;
    std::string     apn;   // empty = don't override
    std::string     pin;   // empty = don't override
};

static gboolean idle_dispatch_connect(gpointer data) {
    auto* d = static_cast<IdleDispatch*>(data);
    if (!d->apn.empty()) d->ctrl->cfg.apn = d->apn;
    if (!d->pin.empty()) d->ctrl->cfg.pin = d->pin;
    d->ctrl->inject(d->event);
    d->pub->publish_response(d->correlation_id, true, "CONNECTING", "connect requested");
    delete d;
    return G_SOURCE_REMOVE;
}

static gboolean idle_dispatch_disconnect(gpointer data) {
    auto* d = static_cast<IdleDispatch*>(data);
    d->ctrl->inject(d->event);
    d->pub->publish_response(d->correlation_id, true, "DISCONNECTING", "disconnect requested");
    delete d;
    return G_SOURCE_REMOVE;
}

static gboolean idle_dispatch_status(gpointer data) {
    auto* d = static_cast<IdleDispatch*>(data);
    std::string st = state_name(d->ctrl->sm_state());
    d->pub->publish_response(d->correlation_id, true, st, "");
    delete d;
    return G_SOURCE_REMOVE;
}

static gboolean idle_dispatch_metrics(gpointer data) {
    auto* d = static_cast<IdleDispatch*>(data);
    d->pub->publish_telemetry(d->ctrl->last_modem_data, d->ctrl->cfg);
    d->pub->publish_response(d->correlation_id, true,
                             state_name(d->ctrl->sm_state()), "telemetry published");
    delete d;
    return G_SOURCE_REMOVE;
}

void CommandHandler::dispatch(const std::string& data) {
    qmiagent::QmiCommand cmd;
    if (!cmd.ParseFromString(data)) {
        LOG_W("[kafka] failed to parse QmiCommand");
        return;
    }

    LOG_I("[kafka] command action=%d corr=%s", cmd.action(), cmd.correlation_id().c_str());

    switch (cmd.action()) {
    case qmiagent::QmiCommand::CONNECT: {
        auto* d = new IdleDispatch{&ctrl_, &pub_, Event::START, cmd.correlation_id(),
                                   cmd.apn(), cmd.pin()};
        g_idle_add(idle_dispatch_connect, d);
        break;
    }
    case qmiagent::QmiCommand::DISCONNECT: {
        auto* d = new IdleDispatch{&ctrl_, &pub_, Event::DISCONNECT_REQ, cmd.correlation_id()};
        g_idle_add(idle_dispatch_disconnect, d);
        break;
    }
    case qmiagent::QmiCommand::GET_STATUS: {
        auto* d = new IdleDispatch{&ctrl_, &pub_, {}, cmd.correlation_id()};
        g_idle_add(idle_dispatch_status, d);
        break;
    }
    case qmiagent::QmiCommand::GET_METRICS: {
        auto* d = new IdleDispatch{&ctrl_, &pub_, {}, cmd.correlation_id()};
        g_idle_add(idle_dispatch_metrics, d);
        break;
    }
    default:
        LOG_W("[kafka] unknown command action: %d", cmd.action());
        pub_.publish_response(cmd.correlation_id(), false, "", "unknown action");
        break;
    }
}

} // namespace uqmi
