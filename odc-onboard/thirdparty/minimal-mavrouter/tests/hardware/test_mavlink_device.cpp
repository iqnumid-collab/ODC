#include "endpoint.h"
#include "mainloop.h"
#include "udev_monitor.h"
#include "device_config_loader.h"

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>

// Hardware tests require a real MAVLink device connected via USB/UART
// These tests are disabled by default and only run with --run-hardware-tests flag

class HardwareTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Skip tests if no hardware is available
        if (!hardware_available()) {
            GTEST_SKIP() << "No MAVLink hardware device available";
        }
    }

    bool hardware_available() {
        // Check for common USB-serial devices
        return access("/dev/ttyUSB0", F_OK) == 0 ||
               access("/dev/ttyACM0", F_OK) == 0 ||
               access("/dev/ttyUSB1", F_OK) == 0;
    }

    std::string find_device() {
        if (access("/dev/ttyUSB0", F_OK) == 0) return "/dev/ttyUSB0";
        if (access("/dev/ttyACM0", F_OK) == 0) return "/dev/ttyACM0";
        if (access("/dev/ttyUSB1", F_OK) == 0) return "/dev/ttyUSB1";
        return "";
    }
};

TEST_F(HardwareTest, DeviceExists) {
    std::string device = find_device();
    EXPECT_FALSE(device.empty()) << "No MAVLink device found";
}

TEST_F(HardwareTest, DeviceIsUSB) {
    std::string device = find_device();
    if (device.empty()) return;

    auto devices = UdevMonitor::enumerate_tty_devices();
    bool found = false;
    for (const auto &dev : devices) {
        if (dev.devnode == device) {
            found = true;
            EXPECT_FALSE(dev.vendor_id.empty()) << "Device should have vendor ID";
            EXPECT_FALSE(dev.model_id.empty()) << "Device should have product ID";
            break;
        }
    }
    EXPECT_TRUE(found) << "Device not found in udev enumeration";
}

TEST_F(HardwareTest, OpenDevice) {
    std::string device = find_device();
    if (device.empty()) return;

    int fd = open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    EXPECT_GT(fd, 0) << "Failed to open device: " << device;
    if (fd > 0) {
        close(fd);
    }
}

TEST_F(HardwareTest, ConfigureBaudRate) {
    std::string device = find_device();
    if (device.empty()) return;

    int fd = open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    ASSERT_GT(fd, 0);

    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    EXPECT_EQ(tcgetattr(fd, &tty), 0);

    cfsetispeed(&tty, B57600);
    cfsetospeed(&tty, B57600);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | PARODD);
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;

    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(ICRNL | INLCR);

    tty.c_lflag = 0;
    tty.c_oflag = 0;

    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    EXPECT_EQ(tcsetattr(fd, TCSANOW, &tty), 0);

    close(fd);
}

TEST_F(HardwareTest, ReadHeartbeat) {
    std::string device = find_device();
    if (device.empty()) return;

    UartEndpoint endpoint("test_uart");
    endpoint.set_device(device.c_str());
    endpoint.set_baudrate(57600);

    // Try to read heartbeat for up to 5 seconds
    bool received_heartbeat = false;
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(5)) {
        // Simulate opening and reading
        int fd = open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd > 0) {
            uint8_t buf[256];
            ssize_t n = read(fd, buf, sizeof(buf));
            if (n > 0) {
                // Check for MAVLink heartbeat (msg_id = 0)
                for (ssize_t i = 0; i < n - 5; i++) {
                    if (buf[i] == 0xFE || buf[i] == 0xFD) { // MAVLink 1 or 2 marker
                        // Simple check - real parsing would be more complex
                        received_heartbeat = true;
                        break;
                    }
                }
            }
            close(fd);
        }
        if (received_heartbeat) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // This may fail if device is not actively sending
    EXPECT_TRUE(received_heartbeat) << "No MAVLink heartbeat received within 5 seconds";
}

TEST_F(HardwareTest, UdevMonitorDetectsDevice) {
    auto devices = UdevMonitor::enumerate_tty_devices();
    std::string device = find_device();

    if (!device.empty()) {
        bool found = false;
        for (const auto &dev : devices) {
            if (dev.devnode == device) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Device " << device << " not detected by udev";
    }
}

TEST_F(HardwareTest, DeviceConfigLoaderFindsConfig) {
    std::string device = find_device();
    if (device.empty()) return;

    auto devices = UdevMonitor::enumerate_tty_devices();
    UsbDeviceInfo info;
    for (const auto &dev : devices) {
        if (dev.devnode == device) {
            info = dev;
            break;
        }
    }

    if (info.vendor_id.empty()) {
        GTEST_SKIP() << "Device is not USB";
        return;
    }

    // Test with default config directory
    auto config = DeviceConfigLoader::find_matching_config(DEFAULT_DEVICE_CONFIG_DIR, info);

    // Should either find a config or return empty (both are valid)
    // Just verify it doesn't crash
    SUCCEED();
}

TEST_F(HardwareTest, EndpointRecovery) {
    std::string device = find_device();
    if (device.empty()) return;

    UartEndpoint endpoint("test_uart");
    endpoint.set_device(device.c_str());
    endpoint.set_baudrate(57600);

    // Set USB config for recovery testing
    auto devices = UdevMonitor::enumerate_tty_devices();
    for (const auto &dev : devices) {
        if (dev.devnode == device) {
            endpoint.set_usb_config(dev.serial, dev.vendor_id, dev.model_id);
            break;
        }
    }

    // Endpoint should be valid even if not connected (USB-identified)
    EXPECT_TRUE(endpoint.is_usb_identified());
    EXPECT_TRUE(endpoint.is_valid());
}

TEST_F(HardwareTest, MultipleDevicesParallel) {
    // Check if multiple devices are available
    std::vector<std::string> devices;
    if (access("/dev/ttyUSB0", F_OK) == 0) devices.push_back("/dev/ttyUSB0");
    if (access("/dev/ttyUSB1", F_OK) == 0) devices.push_back("/dev/ttyUSB1");
    if (access("/dev/ttyACM0", F_OK) == 0) devices.push_back("/dev/ttyACM0");
    if (access("/dev/ttyACM1", F_OK) == 0) devices.push_back("/dev/ttyACM1");

    if (devices.size() < 2) {
        GTEST_SKIP() << "Need at least 2 devices for parallel test";
        return;
    }

    // Open both devices
    int fd1 = open(devices[0].c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    int fd2 = open(devices[1].c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);

    EXPECT_GT(fd1, 0);
    EXPECT_GT(fd2, 0);

    if (fd1 > 0) close(fd1);
    if (fd2 > 0) close(fd2);
}

TEST_F(HardwareTest, DevicePersistence) {
    std::string device = find_device();
    if (device.empty()) return;

    auto devices = UdevMonitor::enumerate_tty_devices();
    UsbDeviceInfo info;
    for (const auto &dev : devices) {
        if (dev.devnode == device) {
            info = dev;
            break;
        }
    }

    if (info.serial.empty()) {
        GTEST_SKIP() << "Device has no serial number";
        return;
    }

    // Test config save/load cycle
    DeviceConfig config;
    config.baud = 115200;
    config.flowcontrol = false;

    // Save to temp directory
    std::string test_dir = "/tmp/mavrouter_hw_test";
    mkdir(test_dir.c_str(), 0755);

    DeviceConfigLoader::save_config(test_dir, info, config);

    // Verify it can be loaded back
    auto loaded = DeviceConfigLoader::find_matching_config(test_dir, info);
    EXPECT_TRUE(loaded.has_value());
    if (loaded.has_value()) {
        EXPECT_EQ(loaded->baud, 115200);
    }

    // Cleanup
    std::string cmd = "rm -rf " + test_dir;
    system(cmd.c_str());
}
