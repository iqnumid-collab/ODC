#include "endpoint.h"
#include "mainloop.h"

#include <gtest/gtest.h>

class TestEndpoint : public Endpoint {
public:
    TestEndpoint()
        : Endpoint{"Test", "test_endpoint"} {};
    ~TestEndpoint() override = default;

    int write_msg(const struct buffer *pbuf) override { return 0; }
    int flush_pending_msgs() override { return -ENOSYS; }
    ssize_t _read_msg(uint8_t *buf, size_t len) override { return 0; }

    void set_sys_comp_ids(std::vector<uint16_t> ids) { _sys_comp_ids = std::move(ids); }
    std::vector<uint16_t> get_sys_comp_ids() const { return _sys_comp_ids; }
};

static uint16_t build_sys_comp_id(unsigned sysid, unsigned compid) {
    return static_cast<uint16_t>(((sysid & 0xff) << 8) | (compid & 0xff));
}

TEST(EndpointTest, HasSysId) {
    TestEndpoint endpoint;

    EXPECT_FALSE(endpoint.has_sys_id(4));

    std::vector<uint16_t> ids;
    ids.push_back(build_sys_comp_id(12, 1));
    endpoint.set_sys_comp_ids(ids);
    EXPECT_TRUE(endpoint.has_sys_id(12));
    EXPECT_FALSE(endpoint.has_sys_id(11));
    EXPECT_FALSE(endpoint.has_sys_id(13));
    EXPECT_FALSE(endpoint.has_sys_id(0));
    EXPECT_FALSE(endpoint.has_sys_id(255));

    ids.push_back(build_sys_comp_id(254, 1));
    endpoint.set_sys_comp_ids(ids);
    EXPECT_TRUE(endpoint.has_sys_id(12));
    EXPECT_TRUE(endpoint.has_sys_id(254));
    EXPECT_FALSE(endpoint.has_sys_id(11));
    EXPECT_FALSE(endpoint.has_sys_id(253));
    EXPECT_FALSE(endpoint.has_sys_id(0));
}

TEST(EndpointTest, HasSysCompId) {
    TestEndpoint endpoint;

    EXPECT_FALSE(endpoint.has_sys_comp_id(4, 0));

    std::vector<uint16_t> ids;
    ids.push_back(build_sys_comp_id(12, 1));
    endpoint.set_sys_comp_ids(ids);
    EXPECT_TRUE(endpoint.has_sys_comp_id(12, 1));
    EXPECT_FALSE(endpoint.has_sys_comp_id(12, 0));
    EXPECT_FALSE(endpoint.has_sys_comp_id(13, 1));
    EXPECT_FALSE(endpoint.has_sys_comp_id(0, 1));

    ids.push_back(build_sys_comp_id(254, 190));
    endpoint.set_sys_comp_ids(ids);
    EXPECT_TRUE(endpoint.has_sys_comp_id(12, 1));
    EXPECT_TRUE(endpoint.has_sys_comp_id(254, 190));
    EXPECT_FALSE(endpoint.has_sys_comp_id(254, 1));
    EXPECT_FALSE(endpoint.has_sys_comp_id(12, 190));
}

TEST(EndpointTest, AcceptBroadcastFromUnknownSource) {
    TestEndpoint endpoint;

    buffer buf = {};
    buf.curr.msg_id = 1;
    buf.curr.target_sysid = 0; // broadcast
    buf.curr.target_compid = 0;
    buf.curr.src_sysid = 5;
    buf.curr.src_compid = 1;

    // Should accept broadcast messages from unknown source
    EXPECT_EQ(endpoint.accept_msg(&buf), Endpoint::AcceptState::Accepted);
}

TEST(EndpointTest, RejectTargetedMessageToUnknownSystem) {
    TestEndpoint endpoint;

    buffer buf = {};
    buf.curr.msg_id = 1;
    buf.curr.target_sysid = 1;
    buf.curr.target_compid = 1;
    buf.curr.src_sysid = 5;
    buf.curr.src_compid = 1;

    // Should reject message targeted to unknown system
    EXPECT_EQ(endpoint.accept_msg(&buf), Endpoint::AcceptState::Rejected);
}

TEST(EndpointTest, AcceptTargetedMessageToKnownSystem) {
    TestEndpoint endpoint;

    // Add target system (not the source)
    std::vector<uint16_t> ids;
    ids.push_back(build_sys_comp_id(1, 1));
    endpoint.set_sys_comp_ids(ids);

    buffer buf = {};
    buf.curr.msg_id = 1;
    buf.curr.target_sysid = 1;
    buf.curr.target_compid = 1;
    buf.curr.src_sysid = 5;
    buf.curr.src_compid = 1;

    // Should accept message targeted to known system from unknown source
    EXPECT_EQ(endpoint.accept_msg(&buf), Endpoint::AcceptState::Accepted);
}

TEST(EndpointTest, RejectMessageFromKnownSource) {
    TestEndpoint endpoint;

    // Add source to known systems
    std::vector<uint16_t> ids;
    ids.push_back(build_sys_comp_id(5, 1));
    endpoint.set_sys_comp_ids(ids);

    buffer buf = {};
    buf.curr.msg_id = 1;
    buf.curr.target_sysid = 0; // broadcast
    buf.curr.target_compid = 0;
    buf.curr.src_sysid = 5;
    buf.curr.src_compid = 1;

    // Should reject message from known source (loop prevention)
    EXPECT_EQ(endpoint.accept_msg(&buf), Endpoint::AcceptState::Rejected);
}

TEST(EndpointTest, FilterAllowedMsgId) {
    TestEndpoint endpoint;

    endpoint.filter_add_allowed_out_msg_id(100);

    buffer buf = {};
    buf.curr.msg_id = 100;
    buf.curr.target_sysid = 0; // broadcast
    buf.curr.target_compid = 0;
    buf.curr.src_sysid = 5;
    buf.curr.src_compid = 1;

    // Should accept allowed message ID from unknown source
    EXPECT_EQ(endpoint.accept_msg(&buf), Endpoint::AcceptState::Accepted);

    // Should filter disallowed message ID
    buf.curr.msg_id = 300;
    EXPECT_EQ(endpoint.accept_msg(&buf), Endpoint::AcceptState::Filtered);
}

TEST(EndpointTest, FilterBlockedMsgId) {
    TestEndpoint endpoint;

    endpoint.filter_add_blocked_out_msg_id(100);

    buffer buf = {};
    buf.curr.msg_id = 200;
    buf.curr.target_sysid = 0; // broadcast
    buf.curr.target_compid = 0;
    buf.curr.src_sysid = 5;
    buf.curr.src_compid = 1;

    // Should accept non-blocked message ID
    EXPECT_EQ(endpoint.accept_msg(&buf), Endpoint::AcceptState::Accepted);

    // Should filter blocked message ID
    buf.curr.msg_id = 100;
    EXPECT_EQ(endpoint.accept_msg(&buf), Endpoint::AcceptState::Filtered);
}

TEST(UartEndpointTest, UsbIdentification) {
    UartEndpoint endpoint("test_uart");

    // Initially not USB identified
    EXPECT_FALSE(endpoint.is_usb_identified());

    // Set USB config
    endpoint.set_usb_config("ABC123", "0403", "6001");
    EXPECT_TRUE(endpoint.is_usb_identified());

    // Test matching
    UsbDeviceInfo info;
    info.serial = "ABC123";
    info.vendor_id = "0403";
    info.model_id = "6001";
    info.devnode = "/dev/ttyUSB0";

    EXPECT_TRUE(endpoint.matches_device(info));

    // Non-matching serial
    info.serial = "WRONG";
    EXPECT_FALSE(endpoint.matches_device(info));

    // Non-matching vendor
    info.serial = "ABC123";
    info.vendor_id = "FFFF";
    EXPECT_FALSE(endpoint.matches_device(info));
}

TEST(UartEndpointTest, DevicePathMatching) {
    UartEndpoint endpoint("test_uart");

    // Not USB identified, should use device path
    EXPECT_FALSE(endpoint.is_usb_identified());

    UsbDeviceInfo info;
    info.devnode = "/dev/ttyUSB0";

    // Without device path set, should not match
    EXPECT_FALSE(endpoint.matches_device(info));
}

TEST(UartEndpointTest, IsValid) {
    UartEndpoint endpoint("test_uart");

    // USB-identified endpoint is valid even when disconnected
    endpoint.set_usb_config("ABC123", "0403", "6001");
    EXPECT_TRUE(endpoint.is_valid());

    // Non-USB endpoint with no fd is not valid
    UartEndpoint endpoint2("test_uart2");
    EXPECT_FALSE(endpoint2.is_valid());
}
