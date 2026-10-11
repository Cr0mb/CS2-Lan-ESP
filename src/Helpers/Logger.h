#pragma once
#include <string>
#include <fstream>
#include <Windows.h>

namespace Log
{
	const std::string LogFile = "Logs.txt";

	inline bool WriteLog(std::string ctx)
	{
		std::ofstream file(LogFile, std::ios::app);
		if (!file.is_open())
			return false;

		file << ctx;
		file.close();

		return true;

	}

	inline void Info(std::string ctx)
	{
	}

	inline void Warning(std::string ctx, bool pause = false)
	{
	}

	inline void Error(std::string ctx, bool fatal = true, bool pause = true)
	{
		// Show error message so user knows what failed
		MessageBoxA(nullptr, ctx.c_str(), "GHax Free - Error", MB_ICONERROR | MB_OK);
		if (fatal)
			exit(-1);
	}

	inline void Fine(std::string ctx)
	{
	}

	inline void Debug(std::string ctx, bool write = false)
	{
#ifdef DBDEBUG
		std::string line = "[Debug]" + ctx + '\n';

		if (write)
			WriteLog(line);
#endif
	}

	inline void Custom(std::string ctx, int color)
	{
	}

	inline void PreviousLine()
	{
	}
}