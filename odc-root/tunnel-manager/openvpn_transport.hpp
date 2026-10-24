#pragma once

#include "openvpn_management.hpp"

#include <openvpn/server/servproto.hpp>
#include <openvpn/transport/server/transbase.hpp>
#include <openvpn/io/io.hpp>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace odc {

class OvpnTransportFactory : public openvpn::TransportServerFactory {
public:
    using Ptr = openvpn::RCPtr<OvpnTransportFactory>;

    OvpnTransportFactory(uint16_t port,
                         openvpn::ServerProto::Factory::Ptr proto_factory);

    openvpn::TransportServer::Ptr new_server_obj(
        openvpn_io::io_context& io_context) override;

private:
    uint16_t port_;
    openvpn::ServerProto::Factory::Ptr proto_factory_;
};

} // namespace odc
