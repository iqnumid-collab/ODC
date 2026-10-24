/*
 * Configuration file client.conf
 * Copyright (c) 2025-2026 Chunghan Yi <chunghan.yi@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include "wgclient/configuration.h"
#include "spdlog/spdlog.h"

static inline void trim(std::string& s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}

static inline std::vector<std::string> split_string(const std::string& s, char delim) {
    std::vector<std::string> result;
    std::string token;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == delim) {
            result.push_back(token);
            token.clear();
        } else {
            token += s[i];
        }
    }
    result.push_back(token);
    return result;
}

/**
 * Parse the client.conf file
 *	key1 = value1
 *	key2 = value2
 *	...
 *	keyN = valueN
 */
bool Config::parse(const std::string& path) {
	std::ifstream config_file(path);
	std::string line;

	if (!config_file.is_open()) {
		spdlog::error("Can't open config file");
		return false;
	}

	while (getline(config_file, line)) {
		std::vector<std::string> parsed_config;
		trim(line);

		if (line.find("#") == 0 or line.empty()) {
			// Ignore comments line
			continue;
		}

		if (line.find("[") == 0) {
			// Ignore section headers like [Interface], [Peer]
			continue;
		}

		parsed_config = split_string(line, '=');

		if (parsed_config.size() == 2) {
			trim(parsed_config[0]);
			trim(parsed_config[1]);

#ifdef DEBUG
			spdlog::debug("### key   ==> {}", parsed_config[0]);
			spdlog::debug("### value ==> {}", parsed_config[1]);
#endif

			_config_tbl[parsed_config[0]] = parsed_config[1];
		} else if (parsed_config.size() == 3) {
			trim(parsed_config[0]);
			trim(parsed_config[1]);
			trim(parsed_config[2]);
			std::string value = parsed_config[1] + "=" + parsed_config[2];

#ifdef DEBUG
			spdlog::debug("### key   ==> {}", parsed_config[0]);
			spdlog::debug("### value ==> {}", value);
#endif

			_config_tbl[parsed_config[0]] = value;
		} else {
			spdlog::error("Can't parse config line: {}", line);
		}
	}
	config_file.close();

	// Map standard WireGuard config keys to expected internal keys
	if (_config_tbl.count("PublicKey") && !_config_tbl.count("this_public_key")) {
		_config_tbl["this_public_key"] = _config_tbl["PublicKey"];
	}
	if (_config_tbl.count("Endpoint") && !_config_tbl.count("this_endpoint_ip")) {
		std::string& ep = _config_tbl["Endpoint"];
		auto colon = ep.find(':');
		if (colon != std::string::npos) {
			_config_tbl["this_endpoint_ip"] = ep.substr(0, colon);
			_config_tbl["this_endpoint_port"] = ep.substr(colon + 1);
		}
	}
	if (_config_tbl.count("AllowedIPs") && !_config_tbl.count("this_allowed_ips")) {
		_config_tbl["this_allowed_ips"] = _config_tbl["AllowedIPs"];
	}
	if (_config_tbl.count("Address") && !_config_tbl.count("this_vpn_ip")) {
		std::string addr = _config_tbl["Address"];
		auto comma = addr.find(',');
		if (comma != std::string::npos) {
			addr = addr.substr(0, comma);
		}
		auto slash = addr.find('/');
		if (slash != std::string::npos) {
			_config_tbl["this_vpn_ip"] = addr.substr(0, slash);
			int prefix = std::stoi(addr.substr(slash + 1));
			uint32_t mask = (prefix == 0) ? 0 : (~0U << (32 - prefix));
			char mask_str[16];
			snprintf(mask_str, sizeof(mask_str), "%u.%u.%u.%u",
				(mask >> 24) & 0xFF, (mask >> 16) & 0xFF,
				(mask >> 8) & 0xFF, mask & 0xFF);
			_config_tbl["this_vpn_netmask"] = mask_str;
		}
	}
	if (_config_tbl.count("PrivateKey") && !_config_tbl.count("this_private_key")) {
		_config_tbl["this_private_key"] = _config_tbl["PrivateKey"];
	}
	if (!_config_tbl.count("this_interface_name")) {
		_config_tbl["this_interface_name"] = "wg0";
	}

	return true;
}

/**
 * Check whether the value for the key is existent or not.
 */
bool Config::contains(const std::string& key) {
	if (_config_tbl.find(key) == _config_tbl.end()) {
		return false;
	} else {
		return true;
	}
}

/**
 * Get the boolean value for the key.
 */
bool Config::getbool(const std::string& key) {
	if (contains(key)) {
		if (_config_tbl[key] == "true" || _config_tbl[key] == "TRUE") {
			return true;
		} else {
			return false;
		}
	} else {
		spdlog::error("No value corresponding to the key.");
		return false;
	}
}

/**
 * Get the integer value for the key.
 */
int Config::getint(const std::string& key) {
	if (contains(key)) {
		return std::stoi(_config_tbl[key]);
	} else {
		spdlog::error("No value corresponding to the key.");
		return 0;
	}
}

/**
 * Get the floating point value for the key.
 */
float Config::getfloat(const std::string& key) {
	if (contains(key)) {
		return std::stof(_config_tbl[key]);
	} else {
		spdlog::error("No value corresponding to the key.");
		return 0.0;
	}
}

/**
 * Get the string value for the key.
 */
std::string Config::getstr(const std::string& key) {
	if (contains(key)) {
		if (_config_tbl[key].find("\"") == std::string::npos) {
			return _config_tbl[key];
		} else {
			return _config_tbl[key].substr(1, _config_tbl[key].length() - 2);
		}
	} else {
		spdlog::error("No value corresponding to the key.");
		return "";
	}
}

/**
 * Set the string value for the key.
 */
bool Config::setstr(const std::string& key, const std::string& value) {
	if (contains(key)) {
		_config_tbl[key] = value;
		return true;
	} else {
		return false;
	}
}
