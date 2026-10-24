/*
 * Copyright (c) 2025-2026 Chunghan Yi <chunghan.yi@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>
#include <vector>

namespace sodium_ae
{
	void initialize_sodium();
	std::string base64_encode(const unsigned char* input, size_t length);
	std::vector<unsigned char> base64_decode(const std::string& input);
	std::vector<unsigned char> encrypt_message(const std::vector<unsigned char>& message,
		const std::vector<unsigned char>& receiver_public_key,
		const std::vector<unsigned char>& sender_secret_key);
	std::vector<unsigned char> decrypt_message(std::vector<unsigned char>& encrypted_message,
		const std::vector<unsigned char>& sender_public_key,
		const std::vector<unsigned char>& receiver_secret_key,
		bool& decrypt_failure);
}
