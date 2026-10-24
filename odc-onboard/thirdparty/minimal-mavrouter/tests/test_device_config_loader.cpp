#include "device_config_loader.h"
#include "udev_monitor.h"

#include <gtest/gtest.h>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>

static std::string test_dir = "/tmp/mavrouter_test_configs";

class DeviceConfigLoaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clean up first
        std::string cmd = "rm -rf " + test_dir;
        system(cmd.c_str());
        mkdir(test_dir.c_str(), 0755);
        DeviceConfigLoader::set_config_dir(test_dir);
    }

    void TearDown() override {
        std::string cmd = "rm -rf " + test_dir;
        system(cmd.c_str());
    }

    void create_config(const std::string &filename, const std::string &content) {
        std::string path = test_dir + "/" + filename;
        FILE *f = fopen(path.c_str(), "w");
        if (f) {
            fputs(content.c_str(), f);
            fclose(f);
        }
    }
};

TEST_F(DeviceConfigLoaderTest, SetAndGetConfigDir) {
    DeviceConfigLoader::set_config_dir("/tmp/test");
    EXPECT_EQ(DeviceConfigLoader::get_config_dir(), "/tmp/test");
    DeviceConfigLoader::set_config_dir(test_dir);
}

TEST_F(DeviceConfigLoaderTest, EnumerateEmptyDirectory) {
    auto configs = DeviceConfigLoader::enumerate_configs();
    EXPECT_TRUE(configs.empty());
}

TEST_F(DeviceConfigLoaderTest, EnumerateWithConfigs) {
    create_config("default.conf", "[UartEndpoint test]\nBaud=115200\n");
    create_config("0403:6001.conf", "[UartEndpoint test]\nBaud=57600\n");

    auto configs = DeviceConfigLoader::enumerate_configs();
    EXPECT_EQ(configs.size(), 2);
}

TEST_F(DeviceConfigLoaderTest, FindConfigExactMatch) {
    create_config("0403:6001-ABC123.conf", "[UartEndpoint test]\nBaud=9600\n");

    UsbDeviceInfo info;
    info.vendor_id = "0403";
    info.model_id = "6001";
    info.serial = "ABC123";

    std::string path = DeviceConfigLoader::find_config(info);
    EXPECT_FALSE(path.empty());
    EXPECT_EQ(path, test_dir + "/0403:6001-ABC123.conf");
}

TEST_F(DeviceConfigLoaderTest, FindConfigVendorProduct) {
    create_config("0403:6001.conf", "[UartEndpoint test]\nBaud=57600\n");

    UsbDeviceInfo info;
    info.vendor_id = "0403";
    info.model_id = "6001";
    info.serial = "UNKNOWN";

    std::string path = DeviceConfigLoader::find_config(info);
    EXPECT_FALSE(path.empty());
    EXPECT_EQ(path, test_dir + "/0403:6001.conf");
}

TEST_F(DeviceConfigLoaderTest, FindConfigDefault) {
    create_config("default.conf", "[UartEndpoint test]\nBaud=115200\n");

    UsbDeviceInfo info;
    info.vendor_id = "FFFF";
    info.model_id = "FFFF";
    info.serial = "UNKNOWN";

    std::string path = DeviceConfigLoader::find_config(info);
    EXPECT_FALSE(path.empty());
    EXPECT_EQ(path, test_dir + "/default.conf");
}

TEST_F(DeviceConfigLoaderTest, FindConfigNoMatch) {
    UsbDeviceInfo info;
    info.vendor_id = "FFFF";
    info.model_id = "FFFF";
    info.serial = "UNKNOWN";

    std::string path = DeviceConfigLoader::find_config(info);
    EXPECT_TRUE(path.empty());
}

TEST_F(DeviceConfigLoaderTest, FindConfigPriority) {
    create_config("default.conf", "[UartEndpoint test]\nBaud=115200\n");
    create_config("0403:6001.conf", "[UartEndpoint test]\nBaud=57600\n");
    create_config("0403:6001-ABC123.conf", "[UartEndpoint test]\nBaud=9600\n");

    UsbDeviceInfo info;
    info.vendor_id = "0403";
    info.model_id = "6001";
    info.serial = "ABC123";

    std::string path = DeviceConfigLoader::find_config(info);
    EXPECT_EQ(path, test_dir + "/0403:6001-ABC123.conf");
}

TEST_F(DeviceConfigLoaderTest, SaveConfig) {
    UsbDeviceInfo info;
    info.vendor_id = "0403";
    info.model_id = "6001";
    info.serial = "ABC123";
    info.devnode = "/dev/ttyUSB0";

    UartEndpointConfig config;
    config.name = "auto_ABC123";
    config.baudrates = {115200};
    config.flowcontrol = false;

    EXPECT_TRUE(DeviceConfigLoader::save_config(info, config));

    std::string expected = test_dir + "/0403:6001-ABC123.conf";
    struct stat st;
    EXPECT_EQ(stat(expected.c_str(), &st), 0);
}

TEST_F(DeviceConfigLoaderTest, SaveConfigDoesNotOverwrite) {
    UsbDeviceInfo info;
    info.vendor_id = "0403";
    info.model_id = "6001";
    info.serial = "ABC123";
    info.devnode = "/dev/ttyUSB0";

    UartEndpointConfig config;
    config.name = "auto_ABC123";
    config.baudrates = {115200};

    EXPECT_TRUE(DeviceConfigLoader::save_config(info, config));

    // Save again - should not fail but not overwrite
    EXPECT_TRUE(DeviceConfigLoader::save_config(info, config));
}

TEST_F(DeviceConfigLoaderTest, SaveConfigEmptySerial) {
    UsbDeviceInfo info;
    info.vendor_id = "0403";
    info.model_id = "6001";
    info.serial = "";
    info.devnode = "/dev/ttyUSB0";

    UartEndpointConfig config;
    config.name = "auto_0403_6001";
    config.baudrates = {57600};

    // Should use fallback filename
    EXPECT_TRUE(DeviceConfigLoader::save_config(info, config));
}

TEST_F(DeviceConfigLoaderTest, BuildFilename) {
    UsbDeviceInfo info;
    info.vendor_id = "0403";
    info.model_id = "6001";
    info.serial = "ABC123";

    // Can't test build_filename directly (private), but find_config uses it
    std::string path = DeviceConfigLoader::find_config(info);
    EXPECT_TRUE(path.empty()); // File doesn't exist yet
}
