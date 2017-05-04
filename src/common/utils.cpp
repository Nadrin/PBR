/*
 * Physically Based Rendering
 * Copyright (c) 2017 Michał Siejak
 */

#include <fstream>
#include <sstream>

#include "utils.hpp"

std::string File::readText(const std::string& filename)
{
	std::ifstream file{filename};
	std::stringstream buffer;
	buffer << file.rdbuf();
	return buffer.str();
}
	
std::vector<char> File::readBinary(const std::string& filename)
{
	std::ifstream file{filename, std::ios::binary | std::ios::ate};
	std::streamsize size = file.tellg();
	file.seekg(0, std::ios::beg);

	std::vector<char> buffer(size);
	file.read(buffer.data(), size);
	return buffer;
}
