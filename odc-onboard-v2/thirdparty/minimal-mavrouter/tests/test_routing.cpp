#include "endpoint.h"
#include "mainloop.h"

#include <gtest/gtest.h>

class TestEndpoint : public Endpoint {
public:
    std::vector<buffer> sent_messages;

    TestEndpoint(const std::string &name)
        : Endpoint{name.c_str(), name.c_str()} {}
    ~TestEndpoint() override = default;

    int write_msg(const struct buffer *pbuf) override {
        sent_messages.push_back(*pbuf);
        return 1;
    }
    int flush_pending_msgs() override { return 0; }
    ssize_t _read_msg(uint8_t *buf, size_t len) override { return 0; }

    void set_sys_comp_ids(std::vector<uint16_t> ids) { _sys_comp_ids = std::move(ids); }
};

static uint16_t build_sys_comp_id(unsigned sysid, unsigned compid) {
    return static_cast<uint16_t>(((sysid & 0xff) << 8) | (compid & 0xff));
}

TEST(RoutingTest, BroadcastMessage) {
    TestEndpoint source("source");
    TestEndpoint target("target");

    buffer buf = {};
    buf.len = 10;
    buf.curr.msg_id = 1;
    buf.curr.target_sysid = 0; // broadcast
    buf.curr.target_compid = 0;
    buf.curr.src_sysid = 1;
    buf.curr.src_compid = 1;

    // Both should accept broadcast from unknown source
    EXPECT_EQ(source.accept_msg(&buf), Endpoint::AcceptState::Accepted);
    EXPECT_EQ(target.accept_msg(&buf), Endpoint::AcceptState::Accepted);
}

TEST(RoutingTest, TargetedMessage) {
    TestEndpoint target("target");

    // Register target system (not the source)
    std::vector<uint16_t> target_ids;
    target_ids.push_back(build_sys_comp_id(2, 1));
    target.set_sys_comp_ids(target_ids);

    buffer buf = {};
    buf.len = 10;
    buf.curr.msg_id = 1;
    buf.curr.target_sysid = 2;
    buf.curr.target_compid = 1;
    buf.curr.src_sysid = 1;
    buf.curr.src_compid = 1;

    // Target should accept message targeted to it from unknown source
    EXPECT_EQ(target.accept_msg(&buf), Endpoint::AcceptState::Accepted);

    // Another endpoint without system 2 should reject
    TestEndpoint other("other");
    EXPECT_EQ(other.accept_msg(&buf), Endpoint::AcceptState::Rejected);
}

TEST(RoutingTest, MessageLoopPrevention) {
    TestEndpoint endpoint("endpoint");

    // Register source system
    std::vector<uint16_t> ids;
    ids.push_back(build_sys_comp_id(5, 1));
    endpoint.set_sys_comp_ids(ids);

    buffer buf = {};
    buf.len = 10;
    buf.curr.msg_id = 1;
    buf.curr.target_sysid = 0; // broadcast
    buf.curr.target_compid = 0;
    buf.curr.src_sysid = 5;
    buf.curr.src_compid = 1;

    // Should reject message from known source (loop prevention)
    EXPECT_EQ(endpoint.accept_msg(&buf), Endpoint::AcceptState::Rejected);
}

TEST(RoutingTest, FilterAllowedMsgId) {
    TestEndpoint endpoint("endpoint");

    endpoint.filter_add_allowed_out_msg_id(100);

    buffer buf = {};
    buf.len = 10;
    buf.curr.msg_id = 100;
    buf.curr.target_sysid = 0; // broadcast
    buf.curr.target_compid = 0;
    buf.curr.src_sysid = 5;
    buf.curr.src_compid = 1;

    // Should accept allowed message ID from unknown source
    EXPECT_EQ(endpoint.accept_msg(&buf), Endpoint::AcceptState::Accepted);

    buf.curr.msg_id = 200;
    EXPECT_EQ(endpoint.accept_msg(&buf), Endpoint::AcceptState::Filtered);
}

TEST(RoutingTest, FilterBlockedMsgId) {
    TestEndpoint endpoint("endpoint");

    endpoint.filter_add_blocked_out_msg_id(100);

    buffer buf = {};
    buf.len = 10;
    buf.curr.msg_id = 200;
    buf.curr.target_sysid = 0; // broadcast
    buf.curr.target_compid = 0;
    buf.curr.src_sysid = 5;
    buf.curr.src_compid = 1;

    // Should accept non-blocked message ID
    EXPECT_EQ(endpoint.accept_msg(&buf), Endpoint::AcceptState::Accepted);

    buf.curr.msg_id = 100;
    EXPECT_EQ(endpoint.accept_msg(&buf), Endpoint::AcceptState::Filtered);
}

TEST(RoutingTest, MultipleEndpointsRouting) {
    TestEndpoint target1("target1");
    TestEndpoint target2("target2");

    // Register different systems on each target (not the source)
    std::vector<uint16_t> ids1;
    ids1.push_back(build_sys_comp_id(10, 1));
    target1.set_sys_comp_ids(ids1);

    std::vector<uint16_t> ids2;
    ids2.push_back(build_sys_comp_id(20, 1));
    target2.set_sys_comp_ids(ids2);

    // Broadcast should reach both from unknown source
    buffer broadcast = {};
    broadcast.len = 10;
    broadcast.curr.msg_id = 1;
    broadcast.curr.target_sysid = 0;
    broadcast.curr.target_compid = 0;
    broadcast.curr.src_sysid = 1;
    broadcast.curr.src_compid = 1;

    EXPECT_EQ(target1.accept_msg(&broadcast), Endpoint::AcceptState::Accepted);
    EXPECT_EQ(target2.accept_msg(&broadcast), Endpoint::AcceptState::Accepted);

    // Targeted to system 10 should only reach target1
    buffer targeted1 = {};
    targeted1.len = 10;
    targeted1.curr.msg_id = 1;
    targeted1.curr.target_sysid = 10;
    targeted1.curr.target_compid = 1;
    targeted1.curr.src_sysid = 1;
    targeted1.curr.src_compid = 1;

    EXPECT_EQ(target1.accept_msg(&targeted1), Endpoint::AcceptState::Accepted);
    EXPECT_EQ(target2.accept_msg(&targeted1), Endpoint::AcceptState::Rejected);

    // Targeted to system 20 should only reach target2
    buffer targeted2 = {};
    targeted2.len = 10;
    targeted2.curr.msg_id = 1;
    targeted2.curr.target_sysid = 20;
    targeted2.curr.target_compid = 1;
    targeted2.curr.src_sysid = 1;
    targeted2.curr.src_compid = 1;

    EXPECT_EQ(target1.accept_msg(&targeted2), Endpoint::AcceptState::Rejected);
    EXPECT_EQ(target2.accept_msg(&targeted2), Endpoint::AcceptState::Accepted);
}

TEST(RoutingTest, HeartbeatMessage) {
    TestEndpoint endpoint("endpoint");

    buffer buf = {};
    buf.len = 10;
    buf.curr.msg_id = MAVLINK_MSG_ID_HEARTBEAT;
    buf.curr.target_sysid = 0;
    buf.curr.target_compid = 0;
    buf.curr.src_sysid = 1;
    buf.curr.src_compid = 1;

    // Heartbeat broadcast should always be accepted from unknown source
    EXPECT_EQ(endpoint.accept_msg(&buf), Endpoint::AcceptState::Accepted);
}
