/*
 * Routines to parse message received from Go client(ex: wireguard windows)
 * Copyright (c) 2025-2026 Chunghan Yi <chunghan.yi@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <map>
#include <cstdint>
#include <limits>
#include <cstring>

#include "wgclient/client.h"
#include "wgclient/common.h"
#include "wgclient/message.h"
#include "spdlog/spdlog.h"

namespace parser
{
bool stringToUint16(const std::string& str, uint16_t& result) {
	try {
		int int_val = std::stoi(str);

		if (int_val >= 0 && int_val <= std::numeric_limits<uint16_t>::max()) {
			result = static_cast<uint16_t>(int_val);
			return true;
		} else {
			// Handle out-of-range error
			return false;
		}
	} catch (const std::invalid_argument&) {
		// Handle invalid argument error
		return false;
	} catch (const std::out_of_range&) {
		// Handle out of range error
		return false;
	}
}

std::vector<std::string> splitString(const std::string& str, const std::string& delimiter) {
	std::vector<std::string> tokens;
	size_t prev_pos = 0;
	size_t current_pos;

	while ((current_pos = str.find(delimiter, prev_pos)) != std::string::npos) {
		tokens.push_back(str.substr(prev_pos, current_pos - prev_pos));
		prev_pos = current_pos + delimiter.length();
	}
	tokens.push_back(str.substr(prev_pos)); // Add the last token

	return tokens;
}

/*
 * <msgtokens>
 *   cmd:=HELLO\n
 *   macaddr:=00-00-00-00-00-00\n
 *   vpnip:=10.1.1.1\n
 *   vpnnetmask:=255.255.255.0\n
 *   publickey:=01234567890123456789012345678901234567890123\n
 *   epip:=192.168.1.1\n
 *   epport:=51280\n
 *   allowedips:=10.1.1.0/24,192.168.1.0\n
*/
bool parse_new_message_string(char* rbuf, message_t* rmsg) {
	std::string text = rbuf;
	std::string delimiter = "\n";
	std::vector<std::string> msgtokens = splitString(text, delimiter);
	int flag = true;

	for (const auto& token : msgtokens) {
		if (token == "") break;
		std::vector<std::string> msgFields = splitString(token, ":=");

		if (msgFields[0] == "cmd") {
			if (msgFields[1] == "HELLO") rmsg->type = AUTOCONN::HELLO;				
			else if (msgFields[1] == "PING") rmsg->type = AUTOCONN::PING;				
			else if (msgFields[1] == "PONG") rmsg->type = AUTOCONN::PONG;				
			else if (msgFields[1] == "OK") rmsg->type = AUTOCONN::OK;				
			else if (msgFields[1] == "NOK") rmsg->type = AUTOCONN::NOK;				
			else if (msgFields[1] == "BYE") rmsg->type = AUTOCONN::BYE;				
			else flag = false;

		} else if (msgFields[0] == "macaddr") {
			const char* mac_string = msgFields[1].c_str();
			unsigned char mac_bytes[6];

			// Use sscanf to parse the string into hexadecimal bytes
			// %hhx is used to read a hexadecimal value into an unsigned char
			// Note: The order of bytes in the array will match the order in the string.
			int result = sscanf(mac_string, "%hhx-%hhx-%hhx-%hhx-%hhx-%hhx",
					&mac_bytes[0], &mac_bytes[1], &mac_bytes[2],
					&mac_bytes[3], &mac_bytes[4], &mac_bytes[5]);

			if (result == 6) {
				for (int i = 0; i < 6; i++) {
					rmsg->mac_addr[i] = mac_bytes[i];
				}
			} else {
				for (int i = 0; i < 6; i++) {
					rmsg->mac_addr[i] = 0xff;
					flag = false;
				}
			}

		} else if (msgFields[0] == "vpnip") {
			if (inet_pton(AF_INET, msgFields[1].c_str(), &(rmsg->vpnIP)) <= 0) {
				flag = false;
			}

		} else if (msgFields[0] == "vpnnetmask") {
			if (inet_pton(AF_INET, msgFields[1].c_str(), &(rmsg->vpnNetmask)) <= 0) {
				flag = false;
			}

		} else if (msgFields[0] == "publickey") {
			int len = msgFields[1].length();
			const uint8_t* p = reinterpret_cast<const uint8_t*>(msgFields[1].c_str());
			std::memset(rmsg->public_key, 0, WG_KEY_LEN_BASE64);
			std::memcpy(rmsg->public_key, p, len);

		} else if (msgFields[0] == "epip") {
			if (inet_pton(AF_INET, msgFields[1].c_str(), &(rmsg->epIP)) <= 0) {
				flag = false;
			}

		} else if (msgFields[0] == "epport") {
			uint16_t num;
			if (stringToUint16(msgFields[1], num)) {
				rmsg->epPort = num;
			} else {
				flag = false;
			}

		} else if (msgFields[0] == "allowedips") {
			int len = msgFields[1].length();
			const uint8_t* p = reinterpret_cast<const uint8_t*>(msgFields[1].c_str());
			std::memset(rmsg->allowed_ips, 0, sizeof(rmsg->allowed_ips));
			if (len < sizeof(rmsg->allowed_ips)) {
				std::memcpy(rmsg->allowed_ips, p, len);
				//spdlog::info("rmsg->allowed_ips ---> [{}]", rmsg->allowed_ips);
			} else {
				flag = false;
			}

		} else {
			//spdlog::warn("Unknown message field [{}]", msgFields[0]);
			flag = false;
		}
	}
	return flag;
}

}
