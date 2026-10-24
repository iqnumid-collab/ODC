#include <openvpn/log/logsimple.hpp>

#include "openvpn_server.hpp"
#include "openvpn_management.hpp"
#include "openvpn_transport.hpp"
#include "openvpn_tun.hpp"

#include "odc/logging.hpp"

#include <openvpn/init/initprocess.hpp>
#include <openvpn/openssl/ssl/sslctx.hpp>
#include <openvpn/ssl/proto.hpp>
#include <openvpn/ssl/proto_context_options.hpp>
#include <openvpn/crypto/crypto_aead.hpp>
#include <openvpn/crypto/cryptodcsel.hpp>
#include <openvpn/crypto/cryptochoose.hpp>
#include <openvpn/ssl/tlsprf.hpp>
#include <openvpn/crypto/ovpnhmac.hpp>
#include <openvpn/crypto/tls_crypt.hpp>
#include <openvpn/random/mtrandapi.hpp>
#include <openvpn/frame/frame_init.hpp>
#include <openvpn/common/options.hpp>
#include <openvpn/common/mode.hpp>
#include <openvpn/log/sessionstats.hpp>
#include <openvpn/server/servproto.hpp>
#include <openvpn/ssl/tlsver.hpp>
#include <openvpn/time/time.hpp>
#include <openvpn/transport/protocol.hpp>

#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>

namespace odc {
namespace {

std::string read_file(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs) return {};
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

} // namespace

struct OpenVpnServer::Impl {
    openvpn::InitProcess::Init init_guard;
    openvpn_io::io_context io{1};
    openvpn::Time now;
    OvpnClientRegistry registry;
    openvpn::ServerProto::Factory::Ptr proto_factory;
    OvpnTunFactory::Ptr tun_factory;
    OvpnManFactory::Ptr man_factory;
    openvpn::TransportServerFactory::Ptr transport_factory;
    openvpn::TransportServer::Ptr transport;
    std::unique_ptr<std::thread> thread;
    std::unique_ptr<openvpn_io::executor_work_guard<openvpn_io::io_context::executor_type>> work;
};

OpenVpnServer::OpenVpnServer(const OvpnServerConfig& config)
    : config_(config) {}

OpenVpnServer::~OpenVpnServer() {
    stop();
}

bool OpenVpnServer::start() {
    if (running_) return true;
    auto logger = get_logger("ovpn_server");

    if (config_.protocol != "udp" && config_.protocol != "UDP") {
        SPDLOG_LOGGER_ERROR(logger, "Only UDP OpenVPN server is supported (got '{}')",
                            config_.protocol);
        if (on_error_) on_error_("OpenVPN server requires protocol=udp");
        return false;
    }

    if (config_.ca_cert_path.empty() || config_.server_cert_path.empty() ||
        config_.server_key_path.empty()) {
        SPDLOG_LOGGER_ERROR(logger, "OpenVPN config incomplete (missing certs/keys)");
        if (on_error_) on_error_("OpenVPN server configuration incomplete");
        return false;
    }

    const std::string ca = read_file(config_.ca_cert_path);
    const std::string cert = read_file(config_.server_cert_path);
    const std::string key = read_file(config_.server_key_path);
    if (ca.empty() || cert.empty() || key.empty()) {
        SPDLOG_LOGGER_ERROR(logger, "Failed to read OpenVPN PEM material");
        if (on_error_) on_error_("Failed to read OpenVPN PEM material");
        return false;
    }

    try {
        impl_ = std::make_unique<Impl>();
        impl_->registry.set_callbacks(on_client_connected_, on_client_disconnected_);

        openvpn::StrongRandomAPI::Ptr rng(new openvpn::SSLLib::RandomAPI());
        openvpn::RandomAPI::Ptr prng(new openvpn::MTRand(static_cast<openvpn::MTRand::result_type>(
            std::chrono::steady_clock::now().time_since_epoch().count())));
        // Frame must be set on SSL Config before new_factory(); otherwise MemQ BIO
        // writes throw frame_uninitialized → BIO_R_INVALID_ARGUMENT during handshake.
        openvpn::Frame::Ptr frame = openvpn::frame_init(true, 1500, 1250, false);
        openvpn::SessionStats::Ptr stats(new openvpn::SessionStats());

        // SSL context (server mode, require client certs)
        openvpn::OpenSSLContext::Config::Ptr ssl_cfg(new openvpn::OpenSSLContext::Config());
        ssl_cfg->set_mode(openvpn::Mode(openvpn::Mode::SERVER));
        ssl_cfg->set_local_cert_enabled(true);
        ssl_cfg->set_rng(new openvpn::SSLLib::RandomAPI());
        ssl_cfg->set_frame(frame);
        // OpenVPN3 server path is more reliable on TLS 1.2 with current clients.
        ssl_cfg->set_tls_version_min(openvpn::TLSVersion::Type::V1_2);
        ssl_cfg->set_tls_version_max(openvpn::TLSVersion::Type::V1_2);
        ssl_cfg->load_ca(ca, true);
        ssl_cfg->load_cert(cert);
        ssl_cfg->load_private_key(key);

        // ProtoConfig from synthetic options (cipher + optional tls-auth)
        std::ostringstream opt_ss;
        opt_ss << "dev tun\n"
               << "proto udp\n"
               << "cipher " << config_.cipher << "\n"
               << "auth SHA1\n";
        if (!config_.tls_auth_key_path.empty()) {
            std::string ta = read_file(config_.tls_auth_key_path);
            if (!ta.empty()) {
                opt_ss << "<tls-auth>\n" << ta << "</tls-auth>\n"
                       << "key-direction 0\n";
            }
        }
        openvpn::OptionList opt = openvpn::OptionList::parse_from_config_static(opt_ss.str(), nullptr);

        openvpn::ProtoContext::ProtoConfig::Ptr pc(new openvpn::ProtoContext::ProtoConfig());
        pc->ssl_factory = ssl_cfg->new_factory();
        // Enable AEAD (and other) ciphers for data-channel use — required before
        // legal_dc_cipher(AES-256-GCM) succeeds (client path does the same).
        openvpn::CryptoAlgs::allow_default_dc_algs<openvpn::SSLLib::CryptoAPI>(
            pc->ssl_factory->libctx(), /*preferred=*/true, /*legacy=*/false);
        pc->dc.set_factory(new openvpn::CryptoDCSelect<openvpn::SSLLib::CryptoAPI>(
            pc->ssl_factory->libctx(), frame, stats, rng));
        pc->tls_auth_factory.reset(new openvpn::CryptoOvpnHMACFactory<openvpn::SSLLib::CryptoAPI>());
        pc->tls_crypt_factory.reset(new openvpn::CryptoTLSCryptFactory<openvpn::SSLLib::CryptoAPI>());
        pc->tls_crypt_metadata_factory.reset(new openvpn::CryptoTLSCryptMetadataFactory());
        pc->tlsprf_factory.reset(new openvpn::CryptoTLSPRFFactory<openvpn::SSLLib::CryptoAPI>());
        openvpn::ProtoContextCompressionOptions pco;
        pc->load(opt, pco, 0, true); // server=true, default key-direction 0
        pc->frame = frame;
        pc->now = &impl_->now;
        pc->rng = rng;
        pc->prng = prng;
        pc->dc_deferred = false;
        pc->set_protocol(openvpn::Protocol(openvpn::Protocol::UDPv4));

        impl_->proto_factory.reset(new openvpn::ServerProto::Factory(impl_->io, *pc));
        impl_->proto_factory->proto_context_config = pc;
        impl_->proto_factory->stats = stats;

        impl_->tun_factory.reset(new OvpnTunFactory(impl_->io, impl_->registry, config_.subnet, "ovpn-root"));
        if (!impl_->tun_factory->open_device()) {
            if (on_error_) on_error_("Failed to create OpenVPN TUN");
            impl_.reset();
            return false;
        }

        impl_->man_factory.reset(new OvpnManFactory(config_, impl_->registry, impl_->tun_factory.get()));
        impl_->man_factory->start();

        impl_->proto_factory->man_factory = impl_->man_factory;
        impl_->proto_factory->tun_factory = impl_->tun_factory;

        impl_->transport_factory.reset(
            new OvpnTransportFactory(config_.listen_port, impl_->proto_factory));
        impl_->transport = impl_->transport_factory->new_server_obj(impl_->io);
        impl_->transport->start();

        impl_->work = std::make_unique<openvpn_io::executor_work_guard<openvpn_io::io_context::executor_type>>(
            impl_->io.get_executor());
        impl_->thread = std::make_unique<std::thread>([this]() {
            try {
                impl_->io.run();
            } catch (const std::exception& e) {
                auto logger = get_logger("ovpn_server");
                SPDLOG_LOGGER_ERROR(logger, "OpenVPN io_context exception: {}", e.what());
                if (on_error_) on_error_(e.what());
            }
        });

        running_ = true;
        SPDLOG_LOGGER_INFO(logger,
                           "OpenVPN in-process server '{}' on UDP/{} subnet={} (TUN {})",
                           config_.id, config_.listen_port, config_.subnet,
                           impl_->tun_factory->ifname());
        return true;
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger, "OpenVPN server start failed: {}", e.what());
        if (on_error_) on_error_(e.what());
        running_ = false;
        if (impl_) {
            if (impl_->transport) {
                try { impl_->transport->stop(); } catch (...) {}
            }
            if (impl_->tun_factory) {
                try { impl_->tun_factory->close_device(); } catch (...) {}
            }
            if (impl_->work) impl_->work.reset();
            if (!impl_->io.stopped()) impl_->io.stop();
            if (impl_->thread && impl_->thread->joinable()) impl_->thread->join();
            impl_.reset();
        }
        return false;
    }
}

void OpenVpnServer::stop() {
    if (!running_ && !impl_) return;
    auto logger = get_logger("ovpn_server");
    running_ = false;

    if (impl_) {
        // Tear down on the io thread so socket/session ops stay single-threaded.
        auto* raw = impl_.get();
        try {
            openvpn_io::post(raw->io, [raw]() {
                try {
                    if (raw->transport) raw->transport->stop();
                } catch (...) {}
                try {
                    if (raw->man_factory) raw->man_factory->stop();
                } catch (...) {}
                try {
                    if (raw->tun_factory) raw->tun_factory->close_device();
                } catch (...) {}
                if (raw->work) raw->work.reset();
            });
        } catch (...) {
            try {
                if (raw->transport) raw->transport->stop();
            } catch (...) {}
            if (raw->work) raw->work.reset();
        }
        if (!raw->io.stopped()) {
            raw->io.stop();
        }
        if (impl_->thread && impl_->thread->joinable()) {
            impl_->thread->join();
        }
        impl_->registry.clear();
        impl_.reset();
    }

    SPDLOG_LOGGER_INFO(logger, "OpenVPN server '{}' stopped", config_.id);
}

std::vector<OvpnClientInfo> OpenVpnServer::list_clients() {
    if (!impl_) return {};
    return impl_->registry.list();
}

bool OpenVpnServer::disconnect_client(const std::string& common_name) {
    if (!impl_) return false;
    return impl_->registry.disconnect_cn(common_name);
}

void OpenVpnServer::poll_clients() {
    // Connect/disconnect events are pushed live from Man; poll is a no-op
    // for edge detection (kept for TunnelManager::run compatibility).
}

} // namespace odc
