#pragma once

#include "core/event.hpp"
#include "core/event_bus.hpp"
#include "thread_orchestrator/orchestrator.hpp"

#include <nlohmann/json.hpp>
#include <glib.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef ODC_HAS_RTSP_COMPRESSOR
#include "HardwareInfo.h"
#include "RtspBridge.h"
#endif

namespace odc {

class RtspCompressorManager {
public:
    RtspCompressorManager(
        EventBus& bus,
        orch::ThreadOrchestrator& orchestrator,
        const nlohmann::json& config)
        : bus_(bus), orchestrator_(orchestrator)
    {
        auto_enabled_ = false;
        if (config.is_object()) {
            if (config.contains("enabled"))
                config_enabled_ = config["enabled"].get<bool>();
        }
    }

    ~RtspCompressorManager() {
        stop_all();
    }

    RtspCompressorManager(const RtspCompressorManager&) = delete;
    RtspCompressorManager& operator=(const RtspCompressorManager&) = delete;

    void start() {
        if (!config_enabled_) return;

        iface_sub_ = bus_.subscribe([this](const Event& evt) {
            if (auto* iface_evt = std::get_if<VpnInterfaceEvent>(&evt)) {
                on_vpn_interface_event(*iface_evt);
            }
        });

        state_sub_ = bus_.subscribe([this](const Event& evt) {
            if (auto* state_evt = std::get_if<VpnStateChangeEvent>(&evt)) {
                on_vpn_state_change(*state_evt);
            }
        });
    }

    void stop_all() {
        if (iface_sub_) {
            bus_.unsubscribe(iface_sub_);
            iface_sub_ = 0;
        }
        if (state_sub_) {
            bus_.unsubscribe(state_sub_);
            state_sub_ = 0;
        }

        std::unique_lock lock(mutex_);
        std::vector<std::string> keys;
        for (const auto& [key, _] : compressors_) {
            keys.push_back(key);
        }
        lock.unlock();

        for (const auto& key : keys) {
            teardown_compressor(key);
        }
    }

    struct CompressorInfo {
        std::string connection_id;
        std::string interface_name;
        std::string target_ip;
        int output_port{8554};
        bool running{false};
    };

    std::vector<CompressorInfo> list_compressors() const {
        std::unique_lock lock(mutex_);
        std::vector<CompressorInfo> result;
        for (const auto& [key, entry] : compressors_) {
            CompressorInfo info;
            info.connection_id = entry->connection_id;
            info.interface_name = entry->interface_name;
            info.target_ip = entry->target_ip;
            info.output_port = entry->output_port;
            info.running = entry->running;
            result.push_back(std::move(info));
        }
        return result;
    }

    bool start_compressor(const std::string& conn_id, const std::string& iface, const std::string& ip) {
        spawn_compressor(conn_id, iface, ip);
        return true;
    }

    bool stop_compressor(const std::string& conn_id) {
        teardown_compressor(conn_id);
        return true;
    }

    void set_auto_enabled(bool enabled) { auto_enabled_ = enabled; }
    bool is_auto_enabled() const { return auto_enabled_; }
    bool is_config_enabled() const { return config_enabled_; }

private:
#ifdef ODC_HAS_RTSP_COMPRESSOR
    struct CompressorEntry {
        std::unique_ptr<RtspBridge> bridge;
        std::thread glib_thread;
        GMainLoop* main_loop{nullptr};
        std::string connection_id;
        std::string interface_name;
        std::string target_ip;
        int output_port{8554};
        std::atomic<bool> running{false};
        std::atomic<bool> should_quit{false};
    };

    void spawn_compressor(const std::string& conn_id, const std::string& iface, const std::string& ip) {
        std::unique_lock lock(mutex_);

        auto it = compressors_.find(conn_id);
        if (it != compressors_.end() && it->second->running) {
            return;
        }
        if (it != compressors_.end()) {
            compressors_.erase(it);
        }

        auto entry = std::make_unique<CompressorEntry>();
        entry->connection_id = conn_id;
        entry->interface_name = iface;
        entry->target_ip = ip;
        entry->output_port = 8554;

        CompressorEntry* raw_entry = entry.get();
        compressors_[conn_id] = std::move(entry);
        lock.unlock();

        raw_entry->glib_thread = std::thread([this, raw_entry, conn_id, ip]() {
            compressor_thread_main(raw_entry, conn_id, ip);
        });

        RtspCompressorStatusEvent status;
        status.connection_id = conn_id;
        status.interface_name = iface;
        status.target_ip = ip;
        status.running = true;
        bus_.publish(Event{std::move(status)});
    }

    void compressor_thread_main(CompressorEntry* entry, const std::string& conn_id, const std::string& /*ip*/) {
        auto loop = g_main_loop_new(nullptr, FALSE);
        {
            std::lock_guard lock(mutex_);
            auto it = compressors_.find(conn_id);
            if (it != compressors_.end()) {
                it->second->main_loop = loop;
            }
        }

        auto bridge = std::make_unique<RtspBridge>(orchestrator_);
        BridgeConfig cfg;
        cfg.input_uri = "rtsp://" + entry->target_ip + ":8554/stream";
        cfg.output_port = entry->output_port;
        cfg.output_path = "/stream";
        cfg.output_codec = "h265";
        cfg.output_bitrate = 800;
        cfg.output_width = 1280;
        cfg.output_height = 720;
        cfg.output_framerate = 30.0;

        bool started = false;
        if (bridge->start(cfg)) {
            started = true;
            {
                std::lock_guard lock(mutex_);
                auto it = compressors_.find(conn_id);
                if (it != compressors_.end()) {
                    it->second->bridge = std::move(bridge);
                    it->second->running = true;
                }
            }
        }

        if (!started) {
            {
                std::lock_guard lock(mutex_);
                auto it = compressors_.find(conn_id);
                if (it != compressors_.end()) {
                    it->second->running = false;
                }
            }
            g_main_loop_unref(loop);
            RtspCompressorStatusEvent status;
            status.connection_id = conn_id;
            status.error_message = "Failed to start compressor";
            bus_.publish(Event{std::move(status)});
            return;
        }

        g_main_loop_run(loop);

        {
            std::lock_guard lock(mutex_);
            auto it = compressors_.find(conn_id);
            if (it != compressors_.end()) {
                (void)it->second->bridge.release();
                it->second->running = false;
            }
        }
        g_main_loop_unref(loop);

        RtspCompressorStatusEvent status;
        status.connection_id = conn_id;
        status.running = false;
        bus_.publish(Event{std::move(status)});
    }

    void teardown_compressor(const std::string& conn_id) {
        GMainLoop* loop_ptr = nullptr;
        bool joinable = false;
        {
            std::unique_lock lock(mutex_);
            auto it = compressors_.find(conn_id);
            if (it == compressors_.end()) return;
            loop_ptr = it->second->main_loop;
            it->second->should_quit = true;
            joinable = it->second->glib_thread.joinable();
        }

        if (loop_ptr) {
            g_main_loop_quit(loop_ptr);
        }

        if (joinable) {
            std::unique_lock lock(mutex_);
            auto it = compressors_.find(conn_id);
            if (it != compressors_.end() && it->second->glib_thread.joinable()) {
                auto t = std::move(it->second->glib_thread);
                lock.unlock();
                t.join();
            }
        }

        {
            std::unique_lock lock(mutex_);
            auto it = compressors_.find(conn_id);
            if (it != compressors_.end()) {
                compressors_.erase(it);
            }
        }
    }

#else
    struct CompressorEntry {
        std::string connection_id;
        std::string interface_name;
        std::string target_ip;
        int output_port{8554};
        std::atomic<bool> running{false};
    };

    void spawn_compressor(const std::string&, const std::string&, const std::string&) {}
    void compressor_thread_main(CompressorEntry*, const std::string&, const std::string&) {}
    void teardown_compressor(const std::string&) {}
#endif

    void on_vpn_interface_event(const VpnInterfaceEvent& evt) {
        switch (evt.action) {
            case VpnInterfaceAction::AddressAdded:
                if (auto_enabled_ && !evt.ip_address.empty() && !evt.ipv6) {
                    spawn_compressor(evt.connection_id, evt.interface_name, evt.ip_address);
                }
                break;
            case VpnInterfaceAction::AddressRemoved:
                if (!evt.ipv6) teardown_compressor(evt.connection_id);
                break;
            case VpnInterfaceAction::InterfaceDeleted:
            case VpnInterfaceAction::InterfaceDown:
                teardown_compressor(evt.connection_id);
                break;
            case VpnInterfaceAction::InterfaceCreated:
            case VpnInterfaceAction::InterfaceUp:
                break;
        }
    }

    void on_vpn_state_change(const VpnStateChangeEvent& evt) {
        if (evt.new_state == VpnState::Disconnected ||
            evt.new_state == VpnState::Error) {
            teardown_compressor(evt.connection_id);
        }
    }

    EventBus& bus_;
    orch::ThreadOrchestrator& orchestrator_;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::unique_ptr<CompressorEntry>> compressors_;

    EventBus::SubscriptionId iface_sub_{0};
    EventBus::SubscriptionId state_sub_{0};

    bool config_enabled_{false};
    bool auto_enabled_{true};
};

}
