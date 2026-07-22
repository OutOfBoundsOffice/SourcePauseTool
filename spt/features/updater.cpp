#include "stdafx.hpp"

#ifdef _WIN32

#include "thirdparty\json_spt_include.hpp"
#include "game_detection.hpp"
#include "interfaces.hpp"
#include "file.hpp"
#include "..\feature.hpp"
#include "..\sptlib-wrapper.hpp"
#include "..\spt-serverplugin.hpp"

#include <Windows.h>
#include <dbghelp.h>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

#define CURL_STATICLIB
extern "C"
{
#include <curl\curl.h>
}

// Update spt
class Updater : public FeatureWrapper<Updater>
{
public:
	enum
	{
		UPDATER_ERROR,
		UPDATER_UP_TO_DATE,
		UPDATER_AHEAD,
		UPDATER_OUTDATED,
	};

	struct ReleaseInfo
	{
		std::string name;
		std::string url;
		std::string body;
		int build;
	} release;

	int CheckUpdate();
	bool ReloadPlugin();
	void ReloadFromPath(const std::string& newPath);
	void UpdatePlugin();
	int GetPluginIndex(); // returns -1 on failure

protected:
	virtual bool ShouldLoadFeature() override;

	virtual void LoadFeature() override;

private:
	CURL* curl;
	curl_slist* slist;

	std::string sptPath;

	bool Prepare(const char* url, int timeOut);
	bool Download(const char* url, const char* path);
	std::string Request(const char* url);
	bool FetchReleaseInfo();
};

static Updater spt_updater;

CON_COMMAND_F(y_spt_check_update, "Check the release information of spt.", FCVAR_DONTRECORD)
{
	int res = spt_updater.CheckUpdate();
	switch (res)
	{
	case Updater::UPDATER_UP_TO_DATE:
		Msg("Already up-to-date.\n");
		break;
	case Updater::UPDATER_AHEAD:
		Msg("Ahead of latest release version.\n");
		break;
	case Updater::UPDATER_OUTDATED:
		Msg("Found newer version. Update with `spt_update`, or download at '%s'\n",
		    spt_updater.release.url.c_str());
		break;
	}
}

CON_COMMAND_F(y_spt_update, "Check and install available update for spt", FCVAR_DONTRECORD)
{
	if (args.ArgC() > 2)
	{
		Msg("Usage: spt_update [force]\n");
		return;
	}
	bool force = false;

	if (args.ArgC() == 2 && std::strcmp(args.Arg(1), "force") == 0)
		force = true;

	int res = spt_updater.CheckUpdate();

	if ((force && res != Updater::UPDATER_ERROR) || (res == Updater::UPDATER_OUTDATED))
	{
		spt_updater.UpdatePlugin();
	}
	else if (res == Updater::UPDATER_UP_TO_DATE)
	{
		Msg("Already up-to-date.\n");
	}
	else if (res == Updater::UPDATER_AHEAD)
	{
		Msg("Ahead of latest release version.\n");
		Msg("Use `spt_update force` to install the latest release version.\n");
	}
}

CON_COMMAND_F(
    y_spt_reload,
    "spt_reload [path]. Reloads the plugin. If a path is given, will try to replace the current loaded plugin with the one from the path.",
    FCVAR_DONTRECORD)
{
	if (args.ArgC() == 1)
		spt_updater.ReloadPlugin();
	else
		spt_updater.ReloadFromPath(args.Arg(1));
}

CON_COMMAND_F(spt_unload, "Unload the plugin.", FCVAR_DONTRECORD)
{
	int index = spt_updater.GetPluginIndex();
	if (index == -1)
	{
		Msg("Could not determine plugin index.\n");
		return;
	}
	char cmd[32];
	snprintf(cmd, sizeof cmd, "plugin_unload %d", index);
	EngineConCmd(cmd);
}

bool Updater::ShouldLoadFeature()
{
	return true;
}

static std::string GetPluginPath()
{
	HMODULE hModule = NULL;
	GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                  (LPCTSTR)GetPluginPath,
	                  &hModule);
	char filename[MAX_PATH + 1];
	GetModuleFileNameA(hModule, filename, MAX_PATH);
	return std::string(filename);
}

void Updater::LoadFeature()
{
	sptPath = GetPluginPath();
	InitCommand(y_spt_check_update);
	InitCommand(y_spt_update);
	InitCommand(spt_unload);
#ifndef OE
	InitCommand(y_spt_reload);
#endif

	// auto-updater & reloader may create this file, try to get rid of it
	std::error_code ec;
	if (fs::exists(TARGET_NAME ".old-auto", ec) && !ec)
		fs::remove(TARGET_NAME ".old-auto", ec);
}

bool Updater::Prepare(const char* url, int timeout)
{
	if (!curl)
	{
		curl = curl_easy_init();
		if (!curl)
		{
			DevMsg("Cannot init curl.\n");
			return false;
		}
		slist = curl_slist_append(slist, "Accept: application/vnd.github.v3+json");
		slist = curl_slist_append(slist, "User-Agent: SourcePauseTool");
	}

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist);

	return true;
}

bool Updater::Download(const char* url, const char* path)
{
	if (!Prepare(url, 60))
		return false;

	FILE* fp = fopen(path, "wb");
	if (!fp)
	{
		DevMsg("Cannot open path '%s'\n", path);
		return false;
	}

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);

	CURLcode res = curl_easy_perform(curl);

	fclose(fp);

	if (res != CURLE_OK)
	{
		DevMsg("Error (%d)\n", res);
		return false;
	}

	return true;
}

std::string Updater::Request(const char* url)
{
	if (!Prepare(url, 10))
	{
		return "";
	}

	curl_easy_setopt(
	    curl,
	    CURLOPT_WRITEFUNCTION,
	    +[](void* ptr, size_t sz, size_t nmemb, std::string* data) -> size_t
	    {
		    data->append((char*)ptr, sz * nmemb);
		    return sz * nmemb;
	    });

	std::string response;
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

	CURLcode res = curl_easy_perform(curl);

	if (res != CURLE_OK)
	{
		DevMsg("Error (%d)\n", res);
		return "";
	}

	return response;
}

bool Updater::FetchReleaseInfo()
{
	std::string data = Request("https://api.github.com/repos/OutOfBoundsOffice/SourcePauseTool/releases/latest");
	if (data.empty())
		return false;

	nlohmann::json res = nlohmann::json::parse(data);
	release.name = res["tag_name"];

	if (release.name.empty())
	{
		DevMsg("Cannot get tag_name.\n");
		return false;
	}

	// Get release build number
	std::string date = res["created_at"];
	if (date.empty())
	{
		DevMsg("Cannot get created_at.\n");
		return false;
	}

	std::tm tm = {};
	std::stringstream ss(date.substr(0, 10));
	ss >> std::get_time(&tm, "%Y-%m-%d");
	std::mktime(&tm);
	if (ss.fail())
		return false;

	int m = tm.tm_mon;
	int d = tm.tm_yday;
	int y = tm.tm_year;

	int build_num;

	build_num = (y - 1) * 365.25;
	build_num += d;
	if (y % 4 == 0 && m > 1)
		build_num += 1;
	build_num -= 35739;

	release.build = build_num;

	// Find download link
	for (auto asset : res["assets"])
	{
		if (asset["name"] == TARGET_NAME)
		{
			release.url = asset["browser_download_url"];
		}
	}

	if (release.url.empty())
	{
		DevMsg("Cannot find download URL for " TARGET_NAME "\n");
		return false;
	}

	// Get release note
	release.body = res["body"];

	return true;
}

bool Updater::ReloadPlugin()
{
	std::error_code ec;
	if (!fs::exists(sptPath, ec) || ec)
	{
		// Prevent renaming the plugin DLL file without replacing a file with the same name back.
		Warning("Failed to reload: plugin path does not exist.\n");
		return false;
	}

	int index = spt_updater.GetPluginIndex();
	if (index == -1)
		return false;

	std::string gamePath = GetGameDir();
	fs::path relPath = fs::relative(sptPath, gamePath, ec);
	if (ec)
	{
		Warning("Failed to get relative path to \"%s\" from game dir.\n", sptPath.c_str());
		return false;
	}
	char cmd[512];
	sprintf(cmd, "plugin_unload %d; plugin_load \"%s\";", index, relPath.string().c_str());
	EngineConCmd(cmd);
	return true;
}

void Updater::ReloadFromPath(const std::string& newPath)
{
	std::error_code ec;
	if (!fs::exists(newPath, ec) || ec)
	{
		Warning("\"%s\" is not a valid path.\n", newPath.c_str());
		return;
	}

	// similar to the updater - rename self, replace original path, and reload

	fs::rename(sptPath, TARGET_NAME ".old-auto", ec);
	if (ec)
	{
		Warning("Failed, could not rename plugin file.\n");
		return;
	}
	fs::copy(newPath, sptPath, ec);
	if (ec)
	{
		// try renaming ourself back
		fs::rename(TARGET_NAME ".old-auto", sptPath, ec);
		if (ec)
		{
			Warning("Failed, could not copy file at \"%s\", plugin location has been moved.\n",
			        newPath.c_str());
		}
		else
		{
			Warning("Failed, could not rename file at \"%s\".\n", newPath.c_str());
		}
	}

#ifdef OE
	Msg("Please restart the game.\n");
#else
	Msg("Reloading plugin...\n");
	if (!ReloadPlugin())
		Warning("An error occurred when reloading, please restart the game.\n");
#endif
}

void Updater::UpdatePlugin()
{
	std::error_code ec;
	fs::path tmpDir = fs::temp_directory_path(ec);
	if (ec)
		tmpDir = TARGET_NAME ".tmp";

	std::string tmpPath = tmpDir.append(TARGET_NAME).string();

	Msg("Downloading " TARGET_NAME " from '%s'\n", release.url.c_str());
	if (!Download(release.url.c_str(), tmpPath.c_str()))
	{
		Msg("An error occurred.\n");
		return;
	}

	// In Windows, you can't delete loaded DLL file, but can rename it.
	fs::rename(sptPath, TARGET_NAME ".old-auto", ec);
	if (ec)
	{
		Warning("Failed, could not rename plugin file.\n");
		return;
	}

	fs::copy(tmpPath, sptPath, ec);
	if (ec)
	{
		// try renaming ourself back
		fs::rename(TARGET_NAME ".old-auto", sptPath, ec);
		if (ec)
		{
			Warning("Failed, could not copy file at \"%s\", plugin location has been moved.\n",
			        tmpPath.c_str());
		}
		else
		{
			Warning("Failed, could not rename file at \"%s\".\n", tmpPath.c_str());
		}
	}
	fs::remove(tmpPath, ec);

	Msg(TARGET_NAME " successfully installed.\n");

#ifdef OE
	Msg("To complete the installation, please restart the game.\n");
#else
	Msg("Reloading plugin...\n");

	if (!ReloadPlugin())
	{
		Msg("An error occurred when reloading. To complete the installation, please restart the game.\n");
	}
#endif
}

int Updater::GetPluginIndex()
{
	auto& plugins = *(CUtlVector<uintptr_t>*)((uint32_t)interfaces::pluginHelpers + 4);
	extern CSourcePauseTool g_SourcePauseTool;

	for (int i = 0; i < plugins.Count(); i++)
		if (*(IServerPluginCallbacks**)(plugins[i] + 132) == &g_SourcePauseTool)
			return i;
	return -1;
}

int Updater::CheckUpdate()
{
	Msg("Fetching latest version information...\n");
	if (!FetchReleaseInfo())
	{
		Warning("An error occurred.\n");
		return UPDATER_ERROR;
	}

	int latestBuildNum = release.build;
	int currentBuildNum = utils::DateToBuildNumber(SPT_VERSION);

	DevMsg("Latest: %d\n", latestBuildNum);
	DevMsg("Current: %d\n", currentBuildNum);
	Msg("[Latest] %s\n", release.name.c_str());

	if (currentBuildNum == latestBuildNum)
		return UPDATER_UP_TO_DATE;

	if (currentBuildNum > latestBuildNum)
		return UPDATER_AHEAD;

	Msg("%s\n\n", release.body.c_str());
	return UPDATER_OUTDATED;
}

#endif
