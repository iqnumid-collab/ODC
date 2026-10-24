#pragma once

#include <cstdio>

#define MAX_PACKET_SIZE 4096

namespace fd_wait
{
	enum Result {
		FAILURE,
		TIMEOUT,
		SUCCESS
	};

	Result waitFor(const FileDescriptor& fileDescriptor, uint32_t timeoutSeconds = 1);
}

namespace common
{
	bool exec(const std::string& cmd, std::vector<std::string>& output_list, std::string& error_text);
}
