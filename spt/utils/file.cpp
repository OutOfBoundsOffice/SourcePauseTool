#include "stdafx.hpp"
#include "file.hpp"
#include "interfaces.hpp"
#include <fstream>

bool FileExists(const std::string& fileName)
{
	std::string dir = fileName;
	std::ifstream is;
	is.open(dir);
	return is.is_open();
}

std::string GetGameDir()
{
	char BUFFER[256];
	if (!interfaces::engine_server)
		return std::string();
	else
	{
		interfaces::engine_server->GetGameDir(BUFFER, ARRAYSIZE(BUFFER));
		return BUFFER;
	}
}
