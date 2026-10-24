/*
 * This file is part of the MAVLink Router project
 *
 * Copyright (C) 2016  Intel Corporation. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "mainloop.h"

#include <assert.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <signal.h>

#include <atomic>
#include <memory>

#include <common/log.h>
#include <common/util.h>

#include "autolog.h"
#include "build_config.h"
#include "device_config_loader.h"
#include "tlog.h"

static std::atomic<bool> global_should_exit{false};

static void exit_signal_handler(int signum)
{
    global_should_exit.store(true, std::memory_order_relaxed);
}

static void setup_signal_handlers()
{
    struct sigaction sa = {};

    sa.sa_flags = SA_NOCLDSTOP;
    sa.sa_handler = exit_signal_handler;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);

    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, nullptr);
}

Mainloop::Mainloop()
{
    static bool signal_handlers_setup = false;
    if (!signal_handlers_setup) {
        setup_signal_handlers();
        signal_handlers_setup = true;
    }
}

Mainloop::~Mainloop()
{
    terminate();
}

int Mainloop::open()
{
    if (epollfd >= 0) {
        return -EEXIST;
    }

    epollfd = epoll_create1(0);
    if (epollfd < 0) {
        log_error("Failed to create epoll: %m");
        return -errno;
    }

    return 0;
}

int Mainloop::add_fd(int fd, void *data, int events) const
{
    struct epoll_event epev;

    epev.events = events;
    epev.data.ptr = data;

    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &epev) < 0) {
        log_error("Failed to add fd to epoll: %m");
        return -errno;
    }

    return 0;
}

int Mainloop::mod_fd(int fd, void *data, int events) const
{
    struct epoll_event epev;

    epev.events = events;
    epev.data.ptr = data;

    if (epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &epev) < 0) {
        log_error("Failed to mod fd in epoll: %m");
        return -errno;
    }

    return 0;
}

int Mainloop::remove_fd(int fd) const
{
    if (epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, nullptr) < 0) {
        log_error("Failed to remove fd from epoll: %m");
        return -errno;
    }

    return 0;
}

void Mainloop::request_exit(int retcode)
{
    _retcode = retcode;
    should_exit.store(true, std::memory_order_relaxed);
}

bool Mainloop::iterate()
{
    const int max_events = 8;
    struct epoll_event events[max_events];

    if (epollfd < 0) {
        return false;
    }

    if (should_exit.load(std::memory_order_relaxed) || global_should_exit.load(std::memory_order_relaxed)) {
        return false;
    }

    int r = epoll_wait(epollfd, events, max_events, 100);
    if (r < 0 && errno == EINTR) {
        return true;
    }

    for (int i = 0; i < r; i++) {
        if (events[i].data.ptr == &g_tcp_fd) {
            handle_tcp_connection();
            continue;
        }

        if (_udev_monitor && events[i].data.ptr == _udev_monitor.get()) {
            _udev_monitor->handle_read();
            continue;
        }

        auto *p = static_cast<Pollable *>(events[i].data.ptr);

        if (events[i].events & EPOLLIN) {
            int rd = p->handle_read();
            if (rd < 0 && !p->is_valid()) {
                should_process_tcp_hangups = true;
            }
        }

        if (events[i].events & EPOLLOUT) {
            if (!p->handle_canwrite()) {
                mod_fd(p->fd, p, EPOLLIN);
            }
        }

        if (events[i].events & EPOLLERR) {
            log_error("poll error for fd %i", p->fd);
            if (p->is_critical()) {
                log_warning("Critical fd %i got error, will attempt recovery", p->fd);
                should_process_tcp_hangups = true;
            } else {
                log_debug("Non-critical fd %i, error is okay.", p->fd);
            }
        }
    }

    if (should_process_tcp_hangups) {
        process_tcp_hangups();
    }

    for (auto &e : g_endpoints) {
        if (!e->is_valid() && e->get_type() == ENDPOINT_TYPE_UART) {
            auto *uart = static_cast<UartEndpoint *>(e.get());
            if (uart->fd < 0 && uart->reopen()) {
                log_info("UART %s recovered, re-adding to epoll", uart->get_display_name().c_str());
                add_fd(uart->fd, uart, EPOLLIN);
            }
        }
    }

    _del_timeouts();

    return !should_exit.load(std::memory_order_relaxed) && !global_should_exit.load(std::memory_order_relaxed);
}

void Mainloop::terminate()
{
    should_exit.store(true, std::memory_order_relaxed);

    if (_log_endpoint != nullptr) {
        _log_endpoint->stop();
    }

    clear_endpoints();

    while (_timeouts != nullptr) {
        Timeout *current = _timeouts;
        _timeouts = current->next;
        remove_fd(current->fd);
        delete current;
    }

    if (epollfd >= 0) {
        ::close(epollfd);
        epollfd = -1;
    }
}

void Mainloop::route_msg(struct buffer *buf)
{
    bool msg_routed = false;

    for (auto &e : g_endpoints) {
        if (!e->is_valid()) {
            continue;
        }

        auto accept_state = e->accept_msg(buf);
        if (accept_state == Endpoint::AcceptState::Rejected) {
            continue;
        }

        if (accept_state == Endpoint::AcceptState::Filtered) {
            continue;
        }

        if (write_msg(e, buf) > 0) {
            msg_routed = true;
        }
    }

    if (!msg_routed) {
        _errors_aggregate.msg_to_unknown++;
    }
}

void Mainloop::handle_tcp_connection()
{
    struct sockaddr_in sockaddr;
    socklen_t sockaddr_len = sizeof(sockaddr);
    int fd;

    fd = accept(g_tcp_fd, (struct sockaddr *)&sockaddr, &sockaddr_len);
    if (fd < 0) {
        log_error("Failed to accept TCP connection: %m");
        return;
    }

    log_info("New TCP connection accepted");

    TcpEndpoint *tcp = new TcpEndpoint{"TCP"};
    tcp->accept(fd);

    if (tcp->is_valid()) {
        add_fd(fd, tcp, EPOLLIN);
        g_endpoints.push_back(std::shared_ptr<Endpoint>{tcp});
    } else {
        delete tcp;
    }
}

int Mainloop::write_msg(const std::shared_ptr<Endpoint> &e, const struct buffer *buf) const
{
    return e->write_msg(buf);
}

void Mainloop::process_tcp_hangups()
{
    should_process_tcp_hangups = false;

    for (auto it = g_endpoints.begin(); it != g_endpoints.end();) {
        auto &e = *it;
        if (!e->is_valid() && e->get_type() == ENDPOINT_TYPE_TCP) {
            remove_fd(e->fd);
            it = g_endpoints.erase(it);
        } else {
            ++it;
        }
    }
}

Timeout *Mainloop::add_timeout(uint32_t timeout_msec, std::function<bool(void *)> cb, const void *data)
{
    Timeout **p = &_timeouts;
    Timeout *t = new Timeout{};

    t->timeout_msec = timeout_msec;
    t->cb = std::move(cb);
    t->data = data;
    t->next = nullptr;

    t->fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (t->fd < 0) {
        log_error("Failed to create timerfd: %m");
        delete t;
        return nullptr;
    }

    struct itimerspec spec = {};
    spec.it_value.tv_sec = timeout_msec / MSEC_PER_SEC;
    spec.it_value.tv_nsec = (timeout_msec % MSEC_PER_SEC) * NSEC_PER_MSEC;
    spec.it_interval = spec.it_value;

    timerfd_settime(t->fd, 0, &spec, nullptr);
    add_fd(t->fd, t, EPOLLIN);

    while (*p) {
        p = &(*p)->next;
    }

    *p = t;
    return t;
}

void Mainloop::del_timeout(Timeout *t)
{
    t->remove_me = true;
}

void Mainloop::mod_timeout(Timeout *t, uint32_t timeout_msec)
{
    struct itimerspec spec = {};

    spec.it_value.tv_sec = timeout_msec / MSEC_PER_SEC;
    spec.it_value.tv_nsec = (timeout_msec % MSEC_PER_SEC) * NSEC_PER_MSEC;
    spec.it_interval = spec.it_value;

    timerfd_settime(t->fd, 0, &spec, nullptr);
    t->timeout_msec = timeout_msec;
}

void Mainloop::_del_timeouts()
{
    Timeout **p = &_timeouts;

    while (*p) {
        Timeout *t = *p;
        if (t->remove_me) {
            *p = t->next;
            remove_fd(t->fd);
            ::close(t->fd);
            delete t;
        } else {
            p = &(*p)->next;
        }
    }
}

bool Mainloop::_log_aggregate_timeout(void *data)
{
    Mainloop *ml = static_cast<Mainloop *>(data);

    if (ml->_errors_aggregate.msg_to_unknown) {
        log_warning("Unknown messages: %u", ml->_errors_aggregate.msg_to_unknown);
        ml->_errors_aggregate.msg_to_unknown = 0;
    }

    return true;
}

void Mainloop::print_statistics()
{
    for (auto &e : g_endpoints) {
        e->print_statistics();
    }
}

bool Mainloop::dedup_check_msg(const buffer *buf)
{
    return _msg_dedup.check_packet(buf->data, buf->len) == Dedup::PacketStatus::NEW_PACKET_OR_TIMED_OUT;
}

bool Mainloop::add_endpoints(const Configuration &config)
{
    _msg_dedup.set_dedup_period(config.dedup_period_ms);

    for (auto &conf : config.uart_configs) {
        if (!UartEndpoint::validate_config(conf)) {
            continue;
        }

        auto *uart = new UartEndpoint{conf.name};
        uart->_mainloop = this;
        if (!uart->setup(conf)) {
            delete uart;
            continue;
        }

        g_endpoints.push_back(std::shared_ptr<Endpoint>{uart});
    }

    for (auto &conf : config.udp_configs) {
        if (!UdpEndpoint::validate_config(conf)) {
            continue;
        }

        auto *udp = new UdpEndpoint{conf.name};
        udp->_mainloop = this;
        if (!udp->setup(conf)) {
            delete udp;
            continue;
        }

        g_endpoints.push_back(std::shared_ptr<Endpoint>{udp});
    }

    for (auto &conf : config.tcp_configs) {
        if (!TcpEndpoint::validate_config(conf)) {
            continue;
        }

        auto *tcp = new TcpEndpoint{conf.name};
        tcp->_mainloop = this;
        if (!tcp->setup(conf)) {
            delete tcp;
            continue;
        }

        g_endpoints.push_back(std::shared_ptr<Endpoint>{tcp});
    }

    if (!config.log_config.logs_dir.empty()) {
        if (config.log_config.mavlink_dialect == LogOptions::MavDialect::Ardupilotmega) {
            auto *log = new AutoLog{config.log_config};
            log->_mainloop = this;
            _log_endpoint = std::shared_ptr<LogEndpoint>{log};
            g_endpoints.push_back(_log_endpoint);
        } else {
            auto *log = new ULog{config.log_config};
            log->_mainloop = this;
            _log_endpoint = std::shared_ptr<LogEndpoint>{log};
            g_endpoints.push_back(_log_endpoint);
        }
    }

    if (config.tcp_port > 0) {
        int ret = tcp_open(config.tcp_port);
        if (ret < 0) {
            log_error("Could not open TCP server on port %lu", config.tcp_port);
        }
    }

    Endpoint::sniffer_sysid = config.sniffer_sysid;

    if (!g_endpoints.empty()) {
        add_timeout(LOG_AGGREGATE_INTERVAL_SEC * MSEC_PER_SEC,
                    std::bind(&Mainloop::_log_aggregate_timeout, this, std::placeholders::_1),
                    this);
    }

    return !g_endpoints.empty();
}

void Mainloop::clear_endpoints()
{
    g_endpoints.clear();
    _log_endpoint.reset();
}

int Mainloop::tcp_open(unsigned long tcp_port)
{
    struct sockaddr_in sockaddr;
    int fd;
    int val = 1;

    fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        log_error("TCP server: %m");
        return -errno;
    }

    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    bzero(&sockaddr, sizeof(sockaddr));
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_addr.s_addr = INADDR_ANY;
    sockaddr.sin_port = htons(tcp_port);

    if (bind(fd, (struct sockaddr *)&sockaddr, sizeof(sockaddr)) < 0) {
        log_error("TCP server bind: %m");
        ::close(fd);
        return -errno;
    }

    if (listen(fd, 4) < 0) {
        log_error("TCP server listen: %m");
        ::close(fd);
        return -errno;
    }

    g_tcp_fd = fd;
    add_fd(fd, &g_tcp_fd, EPOLLIN);

    log_info("TCP server listening on port %lu", tcp_port);

    return 0;
}

void Mainloop::load_device_configs(const Configuration &config)
{
    DeviceConfigLoader::set_config_dir(config.device_config_dir);
}

void Mainloop::link_endpoint_to_groups(std::shared_ptr<Endpoint> new_endpoint)
{
    std::string group_name = new_endpoint->get_group_name();
    if (group_name.empty()) {
        return;
    }

    for (auto &e : g_endpoints) {
        if (e == new_endpoint) {
            continue;
        }

        if (e->get_group_name() == group_name) {
            e->link_group_member(new_endpoint);
            new_endpoint->link_group_member(e);
        }
    }
}

void Mainloop::save_device_config(const UsbDeviceInfo &info, const UartEndpointConfig &config)
{
    DeviceConfigLoader::save_config(info, config);
}

void Mainloop::handle_udev_event(UdevAction action, const UsbDeviceInfo &info)
{
    if (action == UdevAction::Add) {
        UartEndpoint *uart = find_endpoint_for_device(info);
        if (uart) {
            uart->on_device_appeared(info);
            link_endpoint_to_groups(g_endpoints.back());
        }
    } else if (action == UdevAction::Remove) {
        for (auto &e : g_endpoints) {
            if (e->get_type() == ENDPOINT_TYPE_UART) {
                auto *uart = static_cast<UartEndpoint *>(e.get());
                if (uart->matches_device(info)) {
                    uart->on_device_removed();
                }
            }
        }
    }
}

UartEndpoint *Mainloop::find_endpoint_for_device(const UsbDeviceInfo &info)
{
    for (auto &conf : config_for_udev) {
        if (info.devnode == conf.device ||
            (info.serial == conf.usb_serial &&
             info.vendor_id == conf.usb_vendor &&
             info.model_id == conf.usb_product)) {
            auto *uart = new UartEndpoint{conf.name};
            uart->set_usb_config(info.serial, info.vendor_id, info.model_id);
            uart->_mainloop = this;
            g_endpoints.push_back(std::shared_ptr<Endpoint>{uart});
            return uart;
        }
    }

    return nullptr;
}

std::vector<UartEndpointConfig> Mainloop::config_for_udev;
