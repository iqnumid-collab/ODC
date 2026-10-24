#include "mainloop.h"

#include <gtest/gtest.h>

class MainLoopTest : public ::testing::Test {
protected:
    void SetUp() override {
        Mainloop &mainloop = Mainloop::init();
        mainloop.open();
    }

    void TearDown() override {
        Mainloop &mainloop = Mainloop::instance();
        mainloop.request_exit(0);
        mainloop.loop();
        mainloop.teardown();
    }
};

TEST_F(MainLoopTest, InitAndTeardown) {
    Mainloop &mainloop = Mainloop::instance();
    mainloop.request_exit(0);
    int ret = mainloop.loop();
    EXPECT_EQ(0, ret);
}

TEST_F(MainLoopTest, RequestExitWithCode) {
    Mainloop &mainloop = Mainloop::instance();
    mainloop.request_exit(42);
    int ret = mainloop.loop();
    EXPECT_EQ(42, ret);
}

TEST_F(MainLoopTest, Singleton) {
    Mainloop &mainloop1 = Mainloop::instance();
    Mainloop &mainloop2 = Mainloop::instance();
    EXPECT_EQ(&mainloop1, &mainloop2);
}
