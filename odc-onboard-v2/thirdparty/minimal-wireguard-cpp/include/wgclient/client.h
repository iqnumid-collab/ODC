/*
 * Copyright (c) 2025-2026 Chunghan Yi <chunghan.yi@gmail.com>
 * Copyright (c) 2019 Elhay Rauper
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <vector>
#include <queue>
#include <errno.h>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <atomic>
#include "client_observer.h"
#include "pipe_ret_t.h"
#include "file_descriptor.h"
#include "message.h"
#include "configuration.h"

class WgacClient {
public:
	WgacClient();
	~WgacClient();
	pipe_ret_t connectTo(const std::string& address, unsigned short port);

	void start();

	pipe_ret_t sendMsg(unsigned char* msg, size_t size);
	bool handle_message_queue(message_t* pmsg);
	bool send_hello_message(enum AUTOCONN& flag);
	bool send_ping_message(message_t* pmsg);
	bool send_bye_message();

	void setup_wireguard(message_t* rmsg);
	void remove_wireguard(message_t* rmsg);

#ifdef WIREGUARD_C_DAEMON
	void send_ac_vpn_message(message_t* rmsg);
	int send_start_vpn_message(enum AUTOCONN type);
#endif

	Config& getConfig() { return _config; };
	std::string& getServerIp() { return _server_ip; };
	void setServerIp(const std::string Ip) { _server_ip = Ip; };

	bool isConnected() const { return _isConnected; }
	bool isClosed() const { return _isClosed; }
	pipe_ret_t close();

	/* for <PREPARE> stage */
	const std::vector<unsigned char>& getPrepareSecretKey() const { return _prepare_secret_key; }
	void setPrepareSecretKey(uint8_t* key) {
		_prepare_secret_key.assign(key, key + WG_KEY_LEN);
	}
	const std::vector<unsigned char>& getPreparePublicKey() const { return _prepare_public_key; }
	void setPreparePublicKey(uint8_t* key) {
		_prepare_public_key.assign(key, key + WG_KEY_LEN);
	}

	/* for reconnection to server */
	bool shouldRestart();
	void setRestart(bool flag);
	bool isWireguardReady() const { return _isWireguardReady; }

	std::mutex& getMutex() { return _mtx; }
	std::condition_variable& getCond() { return _cond; }

private:
	void initializeSocket();
	void startReceivingMessages();
	void setAddress(const std::string& address, unsigned short port);
	void receiveTask();
	void terminateReceiveThread();

#ifdef WIREGUARD_C_DAEMON
	ssize_t xsendto(int sockfd, const void* buf, size_t len, int flags, const
			struct sockaddr* dest_addr, socklen_t addrlen);
	int send_local_message(ac_message_t* smsg);
#endif

	FileDescriptor _sockfd;
	std::atomic<bool> _isConnected;
	std::atomic<bool> _isClosed;
	struct sockaddr_in _server;
	std::thread* _receiveTask = nullptr;

	/* for <PREPARE> stage */
	std::vector<unsigned char> _prepare_secret_key;  /* client private key */
	std::vector<unsigned char> _prepare_public_key;  /* server public key */

	std::queue<message_t> _msgQueue;
	Config _config;
	std::string _server_ip;

	/* for reconnection to server */
	std::atomic<bool> _flagTerminate = false;
	std::atomic<bool> _isWireguardReady = false;
	std::mutex _mtx;
	std::condition_variable _cond;
};

//////////////////////////////////////////////////////////////////////////////
extern "C" {
	bool initialize_curve25519(char *pubkey, char *privkey);
	bool key_from_base64(uint8_t key[WG_KEY_LEN], const char *base64);
}
