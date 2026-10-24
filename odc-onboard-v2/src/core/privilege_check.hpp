#pragma once

#include <sys/syscall.h>
#include <linux/capability.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <filesystem>

namespace odc {

struct PrivilegeCheckResult {
    bool has_net_admin;
    bool is_root;
    std::vector<std::string> missing_capabilities;
    std::string error_message;
};

struct ConfigFilePermissionResult {
    bool valid;
    bool world_readable;
    bool world_writable;
    mode_t file_mode;
    std::string error_message;
    std::string file_path;
};

inline PrivilegeCheckResult check_privileges() {
    PrivilegeCheckResult result{};

    result.is_root = (getuid() == 0);

    if (result.is_root) {
        result.has_net_admin = true;
        return result;
    }

    struct __user_cap_header_struct hdr;
    hdr.version = _LINUX_CAPABILITY_VERSION_3;
    hdr.pid = 0;

    struct __user_cap_data_struct data[2];

    if (syscall(SYS_capget, &hdr, &data) == -1) {
        result.error_message = "capget syscall failed";
        return result;
    }

    result.has_net_admin = (data[CAP_TO_INDEX(CAP_NET_ADMIN)].effective & CAP_TO_MASK(CAP_NET_ADMIN)) != 0;

    if (!result.has_net_admin) {
        result.missing_capabilities.push_back("CAP_NET_ADMIN");
    }

    return result;
}

inline std::string format_privilege_result(const PrivilegeCheckResult& result) {
    std::string out;

    if (result.is_root) {
        out += "Running as root: YES\n";
        out += "All network privileges available.\n";
        return out;
    }

    out += "Running as root: NO\n";
    out += "CAP_NET_ADMIN: " + std::string(result.has_net_admin ? "YES" : "NO") + "\n";

    if (!result.missing_capabilities.empty()) {
        out += "Missing capabilities: ";
        for (size_t i = 0; i < result.missing_capabilities.size(); ++i) {
            if (i > 0) out += ", ";
            out += result.missing_capabilities[i];
        }
        out += "\n";
    }

    if (!result.error_message.empty()) {
        out += "Error: " + result.error_message + "\n";
    }

    return out;
}

inline ConfigFilePermissionResult check_config_file_permissions(const std::filesystem::path& path) {
    ConfigFilePermissionResult result{};
    result.file_path = path.string();
    result.valid = true;

    if (!std::filesystem::exists(path)) {
        result.valid = false;
        result.error_message = "File does not exist: " + path.string();
        return result;
    }

    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        result.valid = false;
        result.error_message = "stat() failed for: " + path.string();
        return result;
    }

    result.file_mode = st.st_mode;
    result.world_readable = (st.st_mode & S_IROTH) != 0;
    result.world_writable = (st.st_mode & S_IWOTH) != 0;

    if (result.world_readable) {
        result.valid = false;
        result.error_message = "File is world-readable (insecure for credentials): " + path.string();
    }

    if (result.world_writable) {
        result.valid = false;
        if (!result.error_message.empty()) {
            result.error_message += "; ";
        }
        result.error_message += "File is world-writable (insecure): " + path.string();
    }

    return result;
}

inline std::string format_config_permission_result(const ConfigFilePermissionResult& result) {
    std::string out;
    out += "File: " + result.file_path + "\n";
    out += "Valid: " + std::string(result.valid ? "YES" : "NO") + "\n";
    out += "Mode: " + std::to_string(result.file_mode & 07777) + "\n";
    out += "World-readable: " + std::string(result.world_readable ? "YES (INSECURE)" : "NO") + "\n";
    out += "World-writable: " + std::string(result.world_writable ? "YES (INSECURE)" : "NO") + "\n";
    if (!result.error_message.empty()) {
        out += "Error: " + result.error_message + "\n";
    }
    return out;
}

inline bool fix_config_file_permissions(const std::filesystem::path& path, mode_t mode = 0600) {
    if (!std::filesystem::exists(path)) {
        return false;
    }
    return chmod(path.c_str(), mode) == 0;
}

}
