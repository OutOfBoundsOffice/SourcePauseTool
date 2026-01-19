#include "stdafx.hpp"
#include "..\spt-serverplugin.hpp"
#include "SPTLib\sptlib.hpp"
#include "SPTLib\MemUtils.hpp"
#include "interfaces.hpp"
#include "thirdparty\kmp-cpp.hpp"
#include "file.hpp"
#include <atomic>
#include <thread>
#include <future>

#ifdef SSDK2013
#include "convar.hpp"
#endif

#include "game_detection.hpp"

namespace utils
{

	/*
	* Attention to any DoesGameLookLike() callers: it is possible to call these functions too early! If you call
	* them from a static constructor they will (probably) all return false because g_pCVar & friends have not been
	* initialized yet. Once they have the result will be cached.
	*/

	bool DoesGameLookLikePortal()
	{
		// not sure if the cmd + game dir check is necessary, just copying how this function used to work
		static bool cachedGameDir = false, cachedCmd = false, result = false;

		if (!cachedCmd && g_pCVar)
		{
#ifndef OE
			result = g_pCVar->FindCommand("upgrade_portalgun");
			cachedCmd = true;
#endif
		}

		if (cachedCmd)
			return result; // trust the cmd result more than the game directory

		if (!cachedGameDir)
		{
#ifndef OE
			auto game_dir = GetGameDir();
			result = (GetFileName(Convert(game_dir)) == L"portal");
			cachedGameDir = true;
#endif
		}
		return result;
	}

	bool DoesGameLookLikeDMoMM()
	{
		static bool cached = false, result = false;
		if (!cached && g_pCVar)
		{
			result = g_pCVar->FindVar("mm_xana_fov");
			cached = true;
		}
		return result;
	}

	bool DoesGameLookLikeHLS()
	{
		static bool cached = false, result = false;
		if (!cached && g_pCVar)
		{
			result = g_pCVar->FindVar("hl1_ref_db_distance");
			cached = true;
		}
		return result;
	}

	bool DoesGameLookLikeBMSMod()
	{
		static bool cached = false, result = false;
		if (!cached && g_pCVar)
		{
#ifdef SSDK2007
			result = g_pCVar->FindVar("bms_normal_jump_vertical_speed");
			cached = true;
#endif
		}
		return result;
	}

	bool DoesGameLookLikeBMSRetail()
	{
		static bool cached = false, result = false;
		if (!cached && g_pCVar)
		{
			result = g_pCVar->FindVar("bm_announcer");
			cached = true;
		}
		return result;
	}

	bool DoesGameLookLikeBMSLatest()
	{
		static bool cached = false, result = false;
		if (!cached && g_pCVar)
		{
			result = g_pCVar->FindVar("cl_toggle_duck");
			cached = true;
		}
		return result;
	}

	bool DoesGameLookLikeSteampipe()
	{
#ifdef SSDK2013
		return true;
#else
		return false;
#endif
	}

	bool DoesGameLookLikeEstranged()
	{
		static bool cached = false, result = false;
		if (!cached)
		{
#ifndef OE
			auto game_dir = GetGameDir();
			result = (GetFileName(Convert(game_dir)) == L"estrangedact1");
			cached = true;
#endif
		}
		return result;
	}

	const char* GetGameName()
	{
		if (DoesGameLookLikePortal())
			return "Portal 1";
		if (DoesGameLookLikeBMSMod())
			return "BMS Mod";
		if (DoesGameLookLikeBMSRetail())
			return "BMS Retail";
		if (DoesGameLookLikeBMSLatest())
			return "BMS Latest";
		if (DoesGameLookLikeHLS())
			return "HLS";
		if (DoesGameLookLikeEstranged())
			return "Estranged";
		if (DoesGameLookLikeDMoMM())
			return "DMoMM";
		return "HL2";
	}

	static std::future<int> BuildResult;

	int GetBuildNumber()
	{
		static int BuildNumber = -1;
		static bool HasBuildNumber = false;

		if (!HasBuildNumber)
		{
			const char* envBuildNumber = std::getenv("SPT_USE_BUILD_NUMBER");
			if (envBuildNumber)
				BuildNumber = atoi(envBuildNumber);
			else
				BuildNumber = BuildResult.get();

			HasBuildNumber = true;
		}

		return BuildNumber;
	}

	int DateToBuildNumber(const char* date_str)
	{
		const char* months[] = {
		    "Jan",
		    "Feb",
		    "Mar",
		    "Apr",
		    "May",
		    "Jun",
		    "Jul",
		    "Aug",
		    "Sep",
		    "Oct",
		    "Nov",
		    "Dec",
		};

		int monthDays[] = {
		    31,
		    28,
		    31,
		    30,
		    31,
		    30,
		    31,
		    31,
		    30,
		    31,
		    30,
		    31,
		};

		int m = 0, d = 0, y = 0;

		while (m < 11)
		{
			auto month = months[m];
			if (strstr(date_str, month) == date_str)
			{
				break;
			}

			d += monthDays[m];
			m += 1;
		}

		if (date_str[4] == ' ')
		{
			d += (date_str[5] - '0') - 1;
		}
		else
		{
			d += (date_str[4] - '0') * 10 + (date_str[5] - '0') - 1;
		}

		y = std::atoi(date_str + 7) - 1900;

		int build_num;

		build_num = (y - 1) * 365.25;
		build_num += d;
		if (y % 4 == 0 && m > 1)
			build_num += 1;
		build_num -= 35739;
		return build_num;
	}
	void StartBuildNumberSearch()
	{
		BuildResult = std::async(
		    std::launch::async,
		    []()
		    {
			    void* handle;
			    uint8_t* moduleStart;
			    uint8_t* moduleEnd;
			    size_t moduleSize;
			    int build_num = -1;

			    if (MemUtils::GetModuleInfo(L"engine.dll",
			                                &handle,
			                                reinterpret_cast<void**>(&moduleStart),
			                                &moduleSize))
			    {
				    moduleEnd = moduleStart + moduleSize;
				    const char* BUILD_STRING = "Exe build:";
				    const uint8_t* beginNeedle = reinterpret_cast<const uint8_t*>(BUILD_STRING);
				    const uint8_t* endNeedle = beginNeedle + strlen(BUILD_STRING);

				    int match = kmp::match_first(beginNeedle, endNeedle, moduleStart, moduleEnd);

				    if (match >= 0)
				    {
					    const char* wholeString =
					        reinterpret_cast<const char*>(moduleStart) + match;
					    DevMsg("Found date string: %s\n", wholeString);
					    const char* date_str = wholeString + 20;
					    build_num = DateToBuildNumber(date_str);
				    }

					if (match < 0 || build_num < 0)
				    {
#ifdef SSDK2013
					    Warning("Was unable to find date string! Trying fallback via version command...\n");

						ConCommand_guts* versionCommand =
					        reinterpret_cast<ConCommand_guts*>(g_pCVar->FindCommand("version"));
					    int build_num = -1;

					    if (versionCommand)
					    {
						    uint8_t* buildNumPtrPtr =
						        reinterpret_cast<uint8_t*>(versionCommand->m_fnCommandCallback)
						        + 3;

						    if (buildNumPtrPtr >= moduleStart && buildNumPtrPtr <= moduleEnd)
						    {
							    uint8_t* buildNumPtr =
							        *reinterpret_cast<uint8_t**>(buildNumPtrPtr);

							    if (buildNumPtr >= moduleStart && buildNumPtr <= moduleEnd)
							    {
								    build_num = *reinterpret_cast<int*>(buildNumPtr);
							    }
						    }
					    }
						if (build_num >= 0)
						{
							return build_num;
						}
#else
					    Warning("Was unable to find date string for build number!\n");
#endif
					    Warning("Build information not available\n");
				    }
			    }

			    return build_num;
		    });
	}


	bool DataInModule(void* ptr, size_t nBytes, void* modBase, size_t modSize)
	{
		return ptr >= modBase && (char*)ptr + nBytes <= (char*)modBase + modSize
		       && (char*)ptr + nBytes >= modBase;
	}

} // namespace utils
