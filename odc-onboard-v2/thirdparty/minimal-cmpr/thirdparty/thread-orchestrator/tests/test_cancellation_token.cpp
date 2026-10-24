#include <thread_orchestrator/cancellation_token.hpp>
#include <gtest/gtest.h>

TEST(CancellationToken, InitiallyNotCancelled) {
    orch::CancellationSource src;
    auto token = src.token();
    EXPECT_FALSE(token.is_cancelled());
    EXPECT_FALSE(src.is_cancelled());
}

TEST(CancellationToken, CancelPropagates) {
    orch::CancellationSource src;
    auto t1 = src.token();
    auto t2 = src.token();
    src.cancel();
    EXPECT_TRUE(t1.is_cancelled());
    EXPECT_TRUE(t2.is_cancelled());
    EXPECT_TRUE(src.is_cancelled());
}

TEST(CancellationToken, DefaultTokenNotCancelled) {
    orch::CancellationToken token;
    EXPECT_FALSE(token.is_cancelled());
}
