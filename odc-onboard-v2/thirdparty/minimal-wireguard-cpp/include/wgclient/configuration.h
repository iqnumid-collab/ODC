/*
 * Copyright (c) 2025 Chunghan Yi <chunghan.yi@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>
#include <map>

class Config {
public:
	Config() {};
	~Config() {};

	bool parse(const std::string& path); 
	bool contains(const std::string& key);
	bool getbool(const std::string& key);
	int getint(const std::string& key);
	float getfloat(const std::string& key);
	std::string getstr(const std::string& key);
	bool setstr(const std::string& key, const std::string& value);

private:
	std::map<std::string, std::string> _config_tbl;
};
