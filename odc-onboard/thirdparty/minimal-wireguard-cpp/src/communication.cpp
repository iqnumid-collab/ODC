/*
 * wireguard autoconnect protocol: message send/recv routines
 * Copyright (c) 2025-2026 Chunghan Yi <chunghan.yi@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <iostream>
#include <cstdio>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <chrono>
#include <fstream>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <sodium.h>
#include "wgclient/message.h"
#include "wgclient/client.h"
#include "wgclient/configuration.h"
#include "wgclient/pipe_ret_t.h"
#include "wgclient/common.h"
#include "wgclient/cidr.h"
#include "spdlog/spdlog.h"

/**
 * Get local mac address
 */
void get_local_mac_address(char* macaddr) {
	struct ifreq s;
	int fd, i;
	fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
	if (fd >= 0) {
#ifdef VTYSH
		sprintf((char*)s.ifr_name, "%s", "eth0");
#else
		sprintf((char*)s.ifr_name, "%s", "enp4s0");
#endif
		if (0 == ioctl(fd, SIOCGIFHWADDR, &s)) {
			for (i = 0; i < 6; i++)
				macaddr[i] = (unsigned char) s.ifr_addr.sa_data[i];
		}
		close(fd);
	}
}

bool send_all(int sock, const uint8_t* data, size_t len) {
	size_t sent = 0;
	while (sent < len) {
		ssize_t res = ::send(sock, data + sent, len - sent, 0);
		if (res <= 0) return false;
		sent += res;
	}
	return true;
}

bool recv_all(int sock, uint8_t* data, size_t len) {
	size_t received = 0;
	while (received < len) {
		ssize_t res = ::recv(sock, data + received, len - received, 0);
		if (res <= 0) return false;
		received += res;
	}
	return true;
}

/**
 * Initialize a message with the given fields
 */
static inline void init_smsg(message_t* smsg, enum AUTOCONN type, uint32_t ip, uint32_t mask) {
	std::memset(smsg, 0, sizeof(message_t));
	smsg->type = type;
	get_local_mac_address(reinterpret_cast<char*>(smsg->mac_addr));
	smsg->vpnIP.s_addr = ip;
	smsg->vpnNetmask.s_addr = mask;
}

/**
 * Send a HELLO message and receive an HELLO/NOK message
 */
bool WgacClient::send_hello_message(enum AUTOCONN& flag) {
	message_t smsg;

	init_smsg(&smsg, AUTOCONN::HELLO, 0, 0);

	std::memcpy(smsg.public_key, _config.getstr("this_public_key").c_str(), WG_KEY_LEN_BASE64);

	if (inet_pton(AF_INET, _config.getstr("this_endpoint_ip").c_str(), &(smsg.epIP)) != 1) {
		spdlog::warn("inet_pton(this_endpoint_ip) failed.");
		return false;
	}
	smsg.epPort = _config.getint("this_endpoint_port");
	std::memcpy(smsg.allowed_ips, _config.getstr("this_allowed_ips").c_str(), 256);

	pipe_ret_t sendRet = sendMsg(reinterpret_cast<unsigned char*>(&smsg), sizeof(message_t));
	if (!sendRet.isSuccessful()) {
		spdlog::debug(">>> Failed to send message.");
		return false;
	} else {
		spdlog::info(">>> HELLO message sent to server.");
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(500));

	//Get the vpn ip allocated from server
	message_t rmsg;
	if (handle_message_queue(&rmsg)) {
		if (rmsg.type == AUTOCONN::HELLO) {
			spdlog::info("<<< HELLO message received.");
			/* save the vpnIP and vpnNetmask come from server */
			char s[16];
			snprintf(s, sizeof(s), "%s", inet_ntoa(rmsg.vpnIP));
			std::string value1(s);
			_config.setstr("this_vpn_ip", value1);

			sprintf(s, "%s", inet_ntoa(rmsg.vpnNetmask));
			std::string value2(s);
			_config.setstr("this_vpn_netmask", value2);

			spdlog::info("--- vpnIP({}/{}) received from server.", value1, value2);
			return true;
		} else if (rmsg.type == AUTOCONN::BYE) {
			spdlog::info("<<< OMG! BYE message received.");
			_isConnected = false;
			flag = AUTOCONN::BYE;
			return false;
		} else {
			spdlog::info("<<< Oops, HELLO message NOT received.");
			return false;
		}
	} else {
		spdlog::info("<<< No message has arrived.");
		return false;
	}
}

/**
 * Send a PING message and receive a PONG/NOK message
 */
bool WgacClient::send_ping_message(message_t* pmsg) {
	message_t smsg;

	init_smsg(&smsg, AUTOCONN::PING, 0, 0);

	//from saved vpn ip !!!
	if (inet_pton(AF_INET, _config.getstr("this_vpn_ip").c_str(), &(smsg.vpnIP)) != 1) {
		spdlog::warn("inet_pton(this_vpn_ip) failed.");
		return false;
	}
	if (inet_pton(AF_INET, _config.getstr("this_vpn_netmask").c_str(), &(smsg.vpnNetmask)) != 1) {
		spdlog::warn("inet_pton(this_vpn_netmask) failed.");
		return false;
	}

	std::memcpy(smsg.public_key, _config.getstr("this_public_key").c_str(), WG_KEY_LEN_BASE64);

	if (inet_pton(AF_INET, _config.getstr("this_endpoint_ip").c_str(), &(smsg.epIP)) != 1) {
		spdlog::warn("inet_pton(this_endpoint_ip) failed.");
		return false;
	}
	smsg.epPort = _config.getint("this_endpoint_port");
	std::memcpy(smsg.allowed_ips, _config.getstr("this_allowed_ips").c_str(), 256);

	pipe_ret_t sendRet = sendMsg(reinterpret_cast<unsigned char*>(&smsg), sizeof(message_t));
	if (!sendRet.isSuccessful()) {
		spdlog::debug(">>> Failed to send message.");
		return false;
	} else {
		spdlog::info(">>> PING message sent to server.");
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(500));

	if (handle_message_queue(pmsg)) {
		if (pmsg->type == AUTOCONN::PONG) {
			spdlog::info("<<< PONG message received.");
			return true;
		} else {
			spdlog::info("<<< Oops, PONG message NOT received.");
			return false;
		}
	} else {
		spdlog::info("<<< No message has arrived.");
		return false;
	}
}

/**
 * Send a BYE message and receive an BYE/NOK message
 */
bool WgacClient::send_bye_message() {
	message_t smsg;

	init_smsg(&smsg, AUTOCONN::BYE, 0, 0);

	if (inet_pton(AF_INET, _config.getstr("this_vpn_ip").c_str(), &(smsg.vpnIP)) != 1) {
		spdlog::warn("inet_pton(this_vpn_ip) failed.");
		return false;
	}
	if (inet_pton(AF_INET, _config.getstr("this_vpn_netmask").c_str(), &(smsg.vpnNetmask)) != 1) {
		spdlog::warn("inet_pton(this_vpn_netmask) failed.");
		return false;
	}
	std::memcpy(smsg.public_key, _config.getstr("this_public_key").c_str(), WG_KEY_LEN_BASE64);

	pipe_ret_t sendRet = sendMsg(reinterpret_cast<unsigned char*>(&smsg), sizeof(message_t));
	if (!sendRet.isSuccessful()) {
		spdlog::debug(">>> Failed to send message.");
		return false;
	} else {
		spdlog::info(">>> BYE message sent to server.");
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(500));

	message_t rmsg;
	if (handle_message_queue(&rmsg)) {
		if (rmsg.type == AUTOCONN::BYE) {
			spdlog::info("<<< BYE message received.");
			remove_wireguard(&rmsg);
			return true;
		} else {
			spdlog::info("<<< Oops, BYE message NOT received.");
			return false;
		}
	} else {
		spdlog::info("<<< Any message NOT arrived.");
		return false;
	}
}

/**
 * Extract a message from message queue.
 */
bool WgacClient::handle_message_queue(message_t* pmsg) {
	time_t t, last_time = 0;
	last_time = time(NULL);

	while (1) {
		if (!_msgQueue.empty()) {
			*pmsg = _msgQueue.front();
			_msgQueue.pop();
			return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		t = time(NULL);
		if (t - last_time > 1) {
			pmsg->type = AUTOCONN::NOK;
			break;
		}
	}
	return false;
}

/**
 * Setup wireguard configuration with the wg tool or vtysh.
 */
void WgacClient::setup_wireguard(message_t* rmsg) {
	char szInfo[512] = {};
	char vpnip_str[32] = {};
	char epip_str[32] = {};
	int ret = 0;

	snprintf(vpnip_str, sizeof(vpnip_str), "%s", inet_ntoa(rmsg->vpnIP));
	snprintf(epip_str, sizeof(epip_str), "%s", inet_ntoa(rmsg->epIP));

	//Let's configure the VPN IP assigned by the server.
	struct in_addr vpnIP, vpnNetmask;
	if (inet_pton(AF_INET, _config.getstr("this_vpn_ip").c_str(), &(vpnIP)) != 1) {
		spdlog::warn("inet_pton(this_vpn_ip) failed.");
	}
	if (inet_pton(AF_INET, _config.getstr("this_vpn_netmask").c_str(), &(vpnNetmask)) != 1) {
		spdlog::warn("inet_pton(this_vpn_netmask) failed.");
	}

	int cidr = 0;
	for (int i=0; i<CIDR_TBL_NUM; i++) {
		if (!strcmp(cidr_tbl[i], inet_ntoa(vpnNetmask))) {
			cidr = i;
			break;
		}
	}

	std::string iface = _config.getstr("this_interface_name");
	std::string privkey_path = _config.getstr("this_private_key");

	// Write private key to temp file
	std::string privkey_file = "/tmp/wg_privkey_" + iface;
	{
		std::ofstream pkf(privkey_file);
		pkf << _config.getstr("this_private_key");
		pkf.close();
		chmod(privkey_file.c_str(), 0600);
	}

	//Setup wireguard interface -------------------------------------------------------------
	snprintf(szInfo, sizeof(szInfo), "ip link delete %s 2>/dev/null", iface.c_str());
	ret = std::system(szInfo);

	snprintf(szInfo, sizeof(szInfo), "ip link add dev %s type wireguard", iface.c_str());
	ret = std::system(szInfo);
	if (ret < 0) {
		spdlog::warn("<%s> failed(ret=%d).", szInfo, ret);
	}

	snprintf(szInfo, sizeof(szInfo), "ip address add dev %s %s/%d", iface.c_str(), inet_ntoa(vpnIP), cidr);
	ret = std::system(szInfo);
	if (ret < 0) {
		spdlog::warn("<%s> failed(ret=%d).", szInfo, ret);
	}

	snprintf(szInfo, sizeof(szInfo), "ip link set up dev %s", iface.c_str());
	ret = std::system(szInfo);
	if (ret < 0) {
		spdlog::warn("<%s> failed(ret=%d).", szInfo, ret);
	}

	snprintf(szInfo, sizeof(szInfo), "wg set %s listen-port 0 private-key %s", iface.c_str(), privkey_file.c_str());
	ret = std::system(szInfo);
	if (ret < 0) {
		spdlog::warn("<%s> failed(ret=%d).", szInfo, ret);
	}
	//---------------------------------------------------------------------------------------

#ifdef VTYSH
	snprintf(szInfo, sizeof(szInfo),
			"wg peer %s allowed-ips %s/32 endpoint %s:%d persistent-keepalive 25",
			rmsg->public_key, vpnip_str, epip_str, rmsg->epPort);

	char xbuf[1024];
	snprintf(xbuf, sizeof(xbuf), "/usr/bin/qrwg/vtysh -e \"%s\"", szInfo);
	ret = std::system(xbuf);
	if (ret < 0) {
		spdlog::warn("<%s> failed(ret=%d).", xbuf, ret);
	}

	snprintf(xbuf, sizeof(xbuf), "/usr/bin/qrwg/vtysh -e \"write\"");
	ret = std::system(xbuf);
	if (ret < 0) {
		spdlog::warn("<%s> failed(ret=%d).", xbuf, ret);
	}

	spdlog::info("--- wireguard rule [{}]", szInfo);
	spdlog::info("--- OK, wireguard setup is complete.");
#else
#ifdef WIREGUARD_C_DAEMON
	send_ac_vpn_message(rmsg);
	send_start_vpn_message(AUTOCONN::START_VPN);
	spdlog::info("--- OK, wireguard setup is complete.");
#else /* WIREGUARD KERNEL */
	std::string error_text;
	std::vector<std::string> output_list;
	std::string allowed_ips = _config.getstr("this_allowed_ips");
	snprintf(szInfo, sizeof(szInfo),
			"wg set %s peer %s allowed-ips %s endpoint %s:%d persistent-keepalive 25 &",
			iface.c_str(), rmsg->public_key, allowed_ips.c_str(), epip_str, rmsg->epPort);

	std::string cmd(szInfo);
	bool exec_result = common::exec(cmd, output_list, error_text);
	if (exec_result) {
		spdlog::info("--- wireguard rule [{}]", szInfo);
		spdlog::info("--- OK, wireguard setup is complete.");
	} else {
		spdlog::warn("{}", error_text);
	}
#endif
#endif

	_isWireguardReady = true;
}

/**
 * Remove a wireguard configuration with the wg tool or vtysh.
 */
void WgacClient::remove_wireguard(message_t* rmsg) {
	char szInfo[256] = {};
	int ret = 0;

#ifdef VTYSH
	snprintf(szInfo, sizeof(szInfo), "no wg peer %s", rmsg->public_key);

	char xbuf[512];
	snprintf(xbuf, sizeof(xbuf), "/usr/bin/qrwg/vtysh -e \"%s\"", szInfo);
	ret = std::system(xbuf);
	if (ret < 0) {
		spdlog::warn("<%s> failed(ret=%d).", xbuf, ret);
	}

	snprintf(xbuf, sizeof(xbuf), "/usr/bin/qrwg/vtysh -e \"write\"");
	ret = std::system(xbuf);
	if (ret < 0) {
		spdlog::warn("<%s> failed(ret=%d).", xbuf, ret);
	}

	spdlog::info("--- wireugard rule [{}]", szInfo);
	spdlog::info("--- OK, wireguard rule is removed.");
#else
	std::string error_text;
	std::vector<std::string> output_list;
	snprintf(szInfo, sizeof(szInfo), "wg set wg0 peer %s remove", rmsg->public_key);

	std::string cmd(szInfo);
	bool exec_result = common::exec(cmd, output_list, error_text);
	if (exec_result) {
		spdlog::info("--- wireugard rule [{}]", szInfo);
		spdlog::info("--- OK, wireguard rule is removed.");
	} else {
		spdlog::warn("{}", error_text);
	}
#endif

	_isWireguardReady = false;
}

/**
 * Start PING-PONG negotiations
 */
void WgacClient::start() {
	message_t rmsg {};
	int count {};
	enum AUTOCONN flag { AUTOCONN::HELLO };

	//step#1: Let's exchange public key
	uint8_t client_pk_base64[WG_KEY_LEN_BASE64] {};
	std::memcpy(client_pk_base64, _config.getstr("this_public_key").c_str(), WG_KEY_LEN_BASE64);

	if (!send_all(_sockfd.get(), client_pk_base64, sizeof(client_pk_base64)-1)) {
		spdlog::warn("Client public key transmission failed");
	}

	// Set socket timeout for server public key reception (3 seconds)
	struct timeval tv;
	tv.tv_sec = 3;
	tv.tv_usec = 0;
	setsockopt(_sockfd.get(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	uint8_t server_pk_base64[WG_KEY_LEN_BASE64] {};
	bool autoconn_supported = recv_all(_sockfd.get(), server_pk_base64, sizeof(server_pk_base64)-1);

	if (!autoconn_supported) {
		spdlog::info("Server does not support autoconnect protocol, using standard WireGuard setup");
		// Fall back to direct WireGuard setup from config
		std::memset(&rmsg, 0, sizeof(rmsg));
		rmsg.type = AUTOCONN::HELLO;
		std::memcpy(rmsg.public_key, _config.getstr("this_public_key").c_str(), WG_KEY_LEN_BASE64);
		inet_pton(AF_INET, _config.getstr("this_endpoint_ip").c_str(), &(rmsg.epIP));
		rmsg.epPort = _config.getint("this_endpoint_port");
		std::memcpy(rmsg.allowed_ips, _config.getstr("this_allowed_ips").c_str(), 256);
		inet_pton(AF_INET, _config.getstr("this_vpn_ip").c_str(), &(rmsg.vpnIP));
		inet_pton(AF_INET, _config.getstr("this_vpn_netmask").c_str(), &(rmsg.vpnNetmask));
		setup_wireguard(&rmsg);
		return;
	}

	//std::cout << "server_pk_base64 --> " << server_pk_base64 << std::endl;

	uint8_t server_pk[crypto_box_PUBLICKEYBYTES] {};
	if (!key_from_base64(server_pk, reinterpret_cast<const char*>(server_pk_base64))) {
		std::cerr << "Public key is not the correct length or format" << std::endl;
		return;
	}
	setPreparePublicKey(server_pk); /* server public key */

	//step#2: Start receiveTask thread
	startReceivingMessages();
	std::this_thread::sleep_for(std::chrono::milliseconds(1000));

	//step#3: Send PING-PONG messages
	while (1) {
		if (send_hello_message(flag)) {   /* PING-PING protocol stage */
			if (send_ping_message(&rmsg)) {
				setup_wireguard(&rmsg);	  /* wireguard setup stage */
				break;
			}
		} else {
			if (flag == AUTOCONN::BYE) break;
			count++;
			if (count > 3) break;
		}
		sleep(10);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////

/* wrapper around sendto for non blocking I/O */
#ifdef WIREGUARD_C_DAEMON
ssize_t WgacClient::xsendto(int sockfd, const void* buf, size_t len, int flags, const
		struct sockaddr* dest_addr, socklen_t addrlen) {
	ssize_t r;
	fd_set set;

	while ((r = sendto(sockfd, buf, len, flags, dest_addr, addrlen)) == -1
			&& (errno == EAGAIN || errno == EWOULDBLOCK)) {
		FD_ZERO(&set);
		FD_SET(sockfd, &set);
		select(sockfd+1, NULL, &set, NULL, NULL);
	}
	return r;
}

unsigned short vpn_event_port { 7177 };

/* wg_autoc --> wireguard-c daemon */
int WgacClient::send_local_message(ac_message_t* smsg) {
	int sd;
	struct sockaddr_in addr;
	struct hostent *hostent;
	int ret = 0;

	sd = socket(PF_INET, SOCK_DGRAM, 0);
	if (sd < 0) {
		spdlog::error("Could not create the socket.");
		return -1;
	}

	hostent = gethostbyname("127.0.0.1");
	addr.sin_family = AF_INET;
	addr.sin_port = htons(vpn_event_port);
	std::memcpy(&addr.sin_addr, hostent->h_addr, hostent->h_length);

	if (xsendto(sd, smsg, sizeof(ac_message_t), 0,
				(struct sockaddr*)&addr, sizeof (addr)) == -1) {
		spdlog::error("xsendto failed.");
		ret = -1;
	}

	::close(sd);
	return ret;
}

void WgacClient::send_ac_vpn_message(message_t* rmsg) {
	ac_message_t xmsg;
	std::memcpy(&xmsg, rmsg, sizeof(message_t));
	xmsg.m.type = AUTOCONN::SEND_VPN_INFORMATION;
	if (inet_pton(AF_INET, _config.getstr("this_vpn_ip").c_str(), &(xmsg.clientIP)) != 1) {
		spdlog::warn("inet_pton(this_vpn_ip) failed.");
		return;
	}

	if (send_local_message(&xmsg) == 0) {
		spdlog::info("||| VPNINFO sent to wireguard-c daemon thru loopback socket");
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}
}

int WgacClient::send_start_vpn_message(enum AUTOCONN type) {
	int ret = 0;

	ac_message_t xmsg;
	if (type == AUTOCONN::START_VPN) {
		spdlog::info("||| The START_VPN event sent to wireguard-c daemon thru loopback socket");
	} else if (type == AUTOCONN::START_VPN_AGAIN) {
		spdlog::info("||| The START_VPN_AGAIN event sent to wireguard-c daemon thru loopback socket");
	} else {
		spdlog::error("Oops, invalid type value passed.");
		return ret;
	}

	std::memset(&xmsg, 0, sizeof(ac_message_t));
	xmsg.m.type = type;
	if (send_local_message(&xmsg) == 0) {
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}
	return ret;
}
#endif
