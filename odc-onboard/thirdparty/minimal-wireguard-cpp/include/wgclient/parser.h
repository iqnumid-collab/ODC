/*
 * Copyright (c) 2025 Chunghan Yi <chunghan.yi@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "message.h"

namespace parser
{
	bool parse_new_message_string(char* rbuf, message_t* rmsg);
}
