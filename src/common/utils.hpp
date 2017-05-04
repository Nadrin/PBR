/*
 * Physically Based Rendering
 * Copyright (c) 2017 Michał Siejak
 */

#pragma once

#include <string>
#include <vector>

class File
{
public:
	static std::string readText(const std::string& filename);
	static std::vector<char> readBinary(const std::string& filename);
};
