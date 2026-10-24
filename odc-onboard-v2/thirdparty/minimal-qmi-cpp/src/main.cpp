#include "controller.h"
#include "kafka/event_publisher.h"
#include "kafka/command_handler.h"
#include "log.h"
#include <glib.h>
#include <csignal>
#include <memory>

static GMainLoop* loop = nullptr;
static uqmi::Controller* ctrl = nullptr;

static void on_signal(int sig) {
    LOG_I("signal %d received, shutting down", sig);
    if (ctrl) ctrl->stop();
    if (loop) g_main_loop_quit(loop);
}

int main(int argc, char** argv) {
    const char* config_path = "/etc/ultima-qmi.conf";
    if (argc > 1) config_path = argv[1];

    uqmi::Config cfg;
    try {
        cfg = uqmi::Config::load(config_path);
    } catch (...) {
        LOG_W("config %s not found, will auto-generate on modem detection", config_path);
        cfg.config_path = config_path;
    }

    LOG_I("ultima-qmi controller starting");
    LOG_I("device: %s  iface: %s  apn: %s",
          cfg.device_path.empty() ? "(auto-detect)" : cfg.device_path.c_str(),
          cfg.iface_name.c_str(),
          cfg.apn.empty() ? "(SIM default)" : cfg.apn.c_str());
    LOG_I("kafka: %s  broker: %s",
          cfg.kafka_enabled ? "enabled" : "disabled", cfg.kafka_broker.c_str());

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    loop = g_main_loop_new(nullptr, FALSE);
    uqmi::Controller controller(std::move(cfg));
    ctrl = &controller;

    // Wire Kafka layer if enabled
    std::unique_ptr<uqmi::EventPublisher> publisher;
    std::unique_ptr<uqmi::CommandHandler> cmd_handler;

    if (controller.cfg.kafka_enabled) {
        publisher   = std::make_unique<uqmi::EventPublisher>(controller.cfg, controller.pool());
        cmd_handler = std::make_unique<uqmi::CommandHandler>(controller.cfg, controller, *publisher);

        controller.set_publisher(publisher.get());
        publisher->start();
        cmd_handler->start();
    }

    controller.start();
    g_main_loop_run(loop);

    // Shutdown in reverse order
    if (cmd_handler) cmd_handler->stop();
    if (publisher)   publisher->stop();

    ctrl = nullptr;
    g_main_loop_unref(loop);
    LOG_I("ultima-qmi controller stopped");
    return 0;
}
