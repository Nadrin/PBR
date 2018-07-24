/*
 * Physically Based Rendering
 * Copyright (c) 2017-2018 Michał Siejak
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

class Utility
{
public:
	template<typename T> static constexpr bool isPowerOfTwo(T value)
	{
		return value != 0 && (value & (value - 1)) == 0;
	}
	template<typename T> static constexpr T roundToPowerOfTwo(T value, int POT)
	{
		return (value + POT - 1) & -POT;
	}
	template<typename T> static constexpr T numMipmapLevels(T width, T height)
	{
		T levels = 1;
		while((width|height) >> levels) {
			++levels;
		}
		return levels;
	}

#if _WIN32
	static std::string convertToUTF8(const std::wstring& wstr);
	static std::wstring convertToUTF16(const std::string& str);
#endif // _WIN32
};
