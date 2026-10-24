#include <thread_orchestrator/detail/unique_function.hpp>
#include <gtest/gtest.h>
#include <memory>
#include <string>

using orch::detail::UniqueFunction;

TEST(UniqueFunction, InvokeSmallLambda) {
    int x = 0;
    UniqueFunction f([&x] { x = 42; });
    ASSERT_TRUE(static_cast<bool>(f));
    f();
    EXPECT_EQ(x, 42);
    EXPECT_FALSE(static_cast<bool>(f)); // one-shot
}

TEST(UniqueFunction, MoveOnly) {
    int x = 0;
    UniqueFunction f([&x] { x = 1; });
    UniqueFunction g = std::move(f);
    EXPECT_FALSE(static_cast<bool>(f));
    EXPECT_TRUE(static_cast<bool>(g));
    g();
    EXPECT_EQ(x, 1);
}

TEST(UniqueFunction, HeapFallback) {
    // Capture enough data to exceed SBO (48 bytes)
    std::string big(200, 'A');
    std::string result;
    UniqueFunction f([big, &result] { result = big; });
    f();
    EXPECT_EQ(result, std::string(200, 'A'));
}

TEST(UniqueFunction, MoveOnlyCallable) {
    auto ptr = std::make_unique<int>(7);
    int result = 0;
    UniqueFunction f([p = std::move(ptr), &result] { result = *p; });
    f();
    EXPECT_EQ(result, 7);
}

TEST(UniqueFunction, DefaultConstructed) {
    UniqueFunction f;
    EXPECT_FALSE(static_cast<bool>(f));
}

TEST(UniqueFunction, MoveAssign) {
    int a = 0, b = 0;
    UniqueFunction f([&a] { a = 1; });
    UniqueFunction g([&b] { b = 1; });
    f = std::move(g);
    f();
    EXPECT_EQ(a, 0);
    EXPECT_EQ(b, 1);
}
