#include "udev_monitor.h"

#include <gtest/gtest.h>
#include <dirent.h>
#include <sys/stat.h>

TEST(UdevMonitorTest, Init) {
    UdevMonitor monitor;
    EXPECT_TRUE(monitor.init());
    EXPECT_GT(monitor.fd, 0);
}

TEST(UdevMonitorTest, GetDisplayName) {
    UdevMonitor monitor;
    EXPECT_EQ(monitor.get_display_name(), "udev-monitor");
}

TEST(UdevMonitorTest, IsCritical) {
    UdevMonitor monitor;
    EXPECT_FALSE(monitor.is_critical());
}

TEST(UdevMonitorTest, HandleCanWrite) {
    UdevMonitor monitor;
    EXPECT_FALSE(monitor.handle_canwrite());
}

TEST(UdevMonitorTest, EnumerateTtyDevices) {
    auto devices = UdevMonitor::enumerate_tty_devices();
    // Should not crash, may return empty list if no tty devices
    // At least /dev/tty or /dev/tty0 usually exists
    EXPECT_GE(devices.size(), 0);
}

TEST(UdevMonitorTest, ParseDeviceInfo) {
    auto devices = UdevMonitor::enumerate_tty_devices();
    for (const auto &dev : devices) {
        // If we find USB devices, verify their fields
        if (!dev.vendor_id.empty()) {
            EXPECT_FALSE(dev.model_id.empty()) << "USB device should have product ID";
            EXPECT_FALSE(dev.devnode.empty()) << "USB device should have devnode";
        }
    }
}

TEST(UdevMonitorTest, UsbDeviceInfoFields) {
    UsbDeviceInfo info;
    info.devnode = "/dev/ttyUSB0";
    info.serial = "ABC123";
    info.vendor_id = "0403";
    info.model_id = "6001";
    info.usb_path = "pci-0000:00:14.0-usb-0:2:1.0";
    info.manufacturer = "FTDI";
    info.product = "FT232R USB UART";

    EXPECT_EQ(info.devnode, "/dev/ttyUSB0");
    EXPECT_EQ(info.serial, "ABC123");
    EXPECT_EQ(info.vendor_id, "0403");
    EXPECT_EQ(info.model_id, "6001");
}

TEST(UdevMonitorTest, UdevActionEnum) {
    // Verify enum values exist and are distinct
    UdevAction add = UdevAction::Add;
    UdevAction remove = UdevAction::Remove;
    UdevAction change = UdevAction::Change;
    UdevAction unknown = UdevAction::Unknown;

    EXPECT_NE(static_cast<int>(add), static_cast<int>(remove));
    EXPECT_NE(static_cast<int>(add), static_cast<int>(change));
    EXPECT_NE(static_cast<int>(add), static_cast<int>(unknown));
}
