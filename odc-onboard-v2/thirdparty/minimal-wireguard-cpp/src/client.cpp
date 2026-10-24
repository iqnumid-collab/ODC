/*
 * Copyright (c) 2025-2026 Chunghan Yi <chunghan.yi@gmail.com>
 * Copyright (c) 2019 Elhay Rauper
 *
 * SPDX-License-Identifier: MIT
 */

#include <cstring>
#include "wgclient/client.h"
#include "wgclient/message.h"
#include "wgclient/common.h"
#include "wgclient/sodium_ae.h"
#include <sodium.h>
#include "wgclient/parser.h"
#include "spdlog/spdlog.h"

//#define DEBUG

WgacClient::WgacClient() {
	_isConnected = false;
	_isClosed = true;
	_prepare_secret_key.resize(32, 0); /* client private key for PREPARE stage */
	_prepare_public_key.resize(32, 0);  /* server public key for PREPARE stage */
}

WgacClient::~WgacClient() {
	close();
}

pipe_ret_t WgacClient::connectTo(const std::string& address, unsigned short port) {
	try {
		initializeSocket();
		setAddress(address, port);
	} catch (const std::runtime_error& error) {
		return pipe_ret_t::failure(error.what());
	}

	const int connectResult = connect(_sockfd.get(), (struct sockaddr*)&_server, sizeof(_server));
	const bool connectionFailed = (connectResult == -1);
	if (connectionFailed) {
		return pipe_ret_t::failure(strerror(errno));
	}

	_isConnected = true;
	_isClosed = false;

	return pipe_ret_t::success();
}

void WgacClient::startReceivingMessages() {
	_receiveTask = new std::thread(&WgacClient::receiveTask, this);
}

void WgacClient::initializeSocket() {
	pipe_ret_t ret;

	_sockfd.set(socket(AF_INET, SOCK_DGRAM, 0));
	const bool socketFailed = (_sockfd.get() == -1);
	if (socketFailed) {
		spdlog::error("socket failed");
		throw std::runtime_error(strerror(errno));
	}
}

void WgacClient::setAddress(const std::string& address, unsigned short port) {
	const int inetSuccess = inet_aton(address.c_str(), &_server.sin_addr);

	if (!inetSuccess) { // inet_addr failed to parse address
                        // if hostname is not in IP strings and dots format, try resolve it
		struct hostent* host;
		struct in_addr** addrList;
		if ((host = gethostbyname(address.c_str())) == nullptr) {
			spdlog::error("gethostbyname faield");
			throw std::runtime_error("Failed to resolve hostname");
		}
		addrList = (struct in_addr**) host->h_addr_list;
		_server.sin_addr = *addrList[0];
	}
	_server.sin_family = AF_INET;
	_server.sin_port = htons(port);
}

// Let's convert message_t structure to string
/* Golang client syntax
	type Message struct {
		Msg_type    string  `cmd:=HELLO\n`
		Mac_addr    string  `macaddr:=00-00-00-00-00-00\n`
		VpnIP       string  `vpnip:=10.1.1.1\n`
		VpnNetmask  string  `vpnnetmask:=255.255.255.0\n`
		Public_key  string  `publickey:=01234567890123456789012345678901234567890123\n`
		EpIp        string  `epip:=192.168.1.1\n`
		EpPort      string  `epport:=51280\n`
		Allowed_ips string  `allowedips:=10.1.1.0/24,192.168.1.0\n
	}
*/
std::string convert_message2string(unsigned char* msg, size_t size) {
	message_t* smsg = reinterpret_cast<message_t*>(msg);
	std::string total_s {}, s {};
	char buffer[512] {};

	if (smsg->type == AUTOCONN::HELLO)
		total_s = "cmd:=HELLO\n";
	else if (smsg->type == AUTOCONN::PING)
		total_s = "cmd:=PING\n";
	else if (smsg->type == AUTOCONN::PONG)
		total_s = "cmd:=PONG\n";
	else if (smsg->type == AUTOCONN::OK)
		total_s = "cmd:=OK\n";
	else if (smsg->type == AUTOCONN::NOK)
		total_s = "cmd:=NOK\n";
	else if (smsg->type == AUTOCONN::BYE)
		total_s = "cmd:=BYE\n";
	else
		total_s = "cmd:=NOK\n";

	snprintf(buffer, sizeof(buffer), "macaddr:=%02X-%02X-%02X-%02X-%02X-%02X\n",
			smsg->mac_addr[0], smsg->mac_addr[1], smsg->mac_addr[2],
			smsg->mac_addr[3], smsg->mac_addr[4], smsg->mac_addr[5]);
	s = buffer;
	total_s += s;

	snprintf(buffer, sizeof(buffer), "vpnip:=%s\n", inet_ntoa(smsg->vpnIP));
	s = buffer;
	total_s += s;

	snprintf(buffer, sizeof(buffer), "vpnnetmask:=%s\n", inet_ntoa(smsg->vpnNetmask));
	s = buffer;
	total_s += s;

	std::string pubkey(reinterpret_cast<const char*>(smsg->public_key));
	total_s = total_s + "publickey:=" + pubkey + "\n";

	snprintf(buffer, sizeof(buffer), "epip:=%s\n", inet_ntoa(smsg->epIP));
	s = buffer;
	total_s += s;

	snprintf(buffer, sizeof(buffer), "epport:=%d\n", smsg->epPort);
	s = buffer;
	total_s += s;

	std::string allowed(reinterpret_cast<const char*>(smsg->allowed_ips));
	total_s = total_s + "allowedips:=" + allowed + "\n";

	return total_s;
}

#ifdef AUTHENTICATED_ENCRYPTION
//Authenticated encryption routines
/**
 * Send a message to server
 */
pipe_ret_t WgacClient::sendMsg(unsigned char* msg, size_t size) {
	std::string total_s = convert_message2string(msg, size);
	const char* buf_ptr = total_s.c_str();

	std::vector<unsigned char> original_message(buf_ptr, buf_ptr + total_s.length());
	std::vector<unsigned char> encrypted_message = sodium_ae::encrypt_message(original_message,
			getPreparePublicKey(), getPrepareSecretKey());

	const size_t sent_bytes = send(_sockfd.get(), encrypted_message.data(), encrypted_message.size(), 0);

	if (sent_bytes < 0) { // send failed
		return pipe_ret_t::failure(strerror(errno));
	}
	if (sent_bytes < encrypted_message.size()) { // not all bytes were sent
		char errorMsg[100];
		sprintf(errorMsg, "Only %lu bytes out of %lu was sent to client", sent_bytes, encrypted_message.size());
		return pipe_ret_t::failure(errorMsg);
	}
	return pipe_ret_t::success();
}

/*
 * Thread routine: Receive a message from server
 */
void WgacClient::receiveTask() {
	while(_isConnected) {
		const fd_wait::Result waitResult = fd_wait::waitFor(_sockfd);

		if (waitResult == fd_wait::Result::FAILURE) {
			spdlog::info("fd_wait::Result::FAILURE !!!");
			_isConnected = false;
			return;
		} else if (waitResult == fd_wait::Result::TIMEOUT) {
			continue;
		}

		unsigned char recv_buf[1024] {};
		const size_t received_bytes = recv(_sockfd.get(), recv_buf, sizeof(recv_buf), 0);
		if (received_bytes < 1) {
			std::string errorMsg;
			if (received_bytes == 0) { //server closed connection
				errorMsg = "Server closed connection";
			} else {
				errorMsg = strerror(errno);
			}
			_isConnected = false;
			return;
		}

		std::vector<unsigned char> encrypted_message(recv_buf, recv_buf + received_bytes);
		bool decrypt_failure = false;
		std::vector<unsigned char> decrypted_message = sodium_ae::decrypt_message(
				encrypted_message, getPreparePublicKey(), getPrepareSecretKey(), decrypt_failure);
		if (!decrypt_failure) {
			char xbuf[1024] {};
			message_t rmsg {};
			memcpy(xbuf, reinterpret_cast<char*>(decrypted_message.data()),
					decrypted_message.size() * sizeof(unsigned char));
			if (!parser::parse_new_message_string(xbuf, &rmsg)) {
				spdlog::error("Failed to parse message string");
				return;
			}
			/* Let's put this message to <message queue>. */
			_msgQueue.push(rmsg);
		}
	}
}
#else //======================================================================================
//No authenticated encryption routines
/**
 * Send a message to server
 */
pipe_ret_t WgacClient::sendMsg(unsigned char* msg, size_t size) {
	std::string total_s = convert_message2string(msg, size);

	const char* buf_ptr = total_s.c_str();
	try {
		const size_t sent_bytes = ::send(_sockfd.get(), buf_ptr, total_s.length(), 0);
	} catch (const std::runtime_error &error) {
		return pipe_ret_t::failure(">>> Oops message sending is failed.");
	}
	return pipe_ret_t::success();
}

/**
 * Thread routine: Receive a message from server
 */
void WgacClient::receiveTask() {
	while(_isConnected) {
		const fd_wait::Result waitResult = fd_wait::waitFor(_sockfd);

		if (waitResult == fd_wait::Result::FAILURE) {
			spdlog::info("fd_wait::Result::FAILURE !!!");
			_isConnected = false;
			return;
		} else if (waitResult == fd_wait::Result::TIMEOUT) {
			continue;
		}

		message_t rmsg {};
		char recv_buf[1024] {};
		const size_t received_bytes = recv(_sockfd.get(), recv_buf, sizeof(recv_buf), 0);
		if (!parser::parse_new_message_string(recv_buf, &rmsg)) {
			spdlog::error("Failed to parse message string");
			return;
		}

		if (received_bytes < 1) {
			std::string errorMsg;
			if (received_bytes == 0) { //server closed connection
				errorMsg = "Server closed connection";
			} else {
				errorMsg = strerror(errno);
			}
			_isConnected = false;
			return;
		} else {
			/* Let's put this message to <message queue>. */
			_msgQueue.push(rmsg);
		}
	}
}
#endif //======================================================================================

void WgacClient::terminateReceiveThread() {
	_isConnected = false;

	if (_receiveTask) {
		_receiveTask->join();
		delete _receiveTask;
		_receiveTask = nullptr;
	}
}

pipe_ret_t WgacClient::close() {
	if (_isClosed) {
		return pipe_ret_t::failure("client is already closed");
	}
	terminateReceiveThread();

	const bool closeFailed = (::close(_sockfd.get()) == -1);
	if (closeFailed) {
		return pipe_ret_t::failure(strerror(errno));
	}
	_isClosed = true;
	return pipe_ret_t::success();
}

void WgacClient::setRestart(bool flag) {
	_flagTerminate = flag;
}

bool WgacClient::shouldRestart() {
	return _flagTerminate;
}
