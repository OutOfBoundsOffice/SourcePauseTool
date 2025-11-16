#include "stdafx.hpp"

#include <chrono>
#include <functional>
#include <sstream>
#include <time.h>

#include "vstdlib\IKeyValuesSystem.h"
#include <SPTLib\Hooks.hpp>
#include "spt-serverplugin.hpp"
#include "..\sptlib-wrapper.hpp"
#include "ent_utils.hpp"
#include "math.hpp"
#include "string_utils.hpp"
#include "game_detection.hpp"
#include "..\features\generic.hpp"
#include "..\features\playerio.hpp"
#include "..\features\tas.hpp"
#include "custom_interfaces\engine_client.hpp"
#include "custom_interfaces\engine_server.hpp"
#include "cvars.hpp"
#include "scripts\srctas_reader.hpp"
#include "..\feature.hpp"
#include "vstdlib\random.h"

#include "cdll_int.h"
#include "eiface.h"
#include "engine\iserverplugin.h"
#include "icliententitylist.h"
#include "tier2\tier2.h"
#include "tier3\tier3.h"
#include "vgui\iinput.h"
#include "vgui\isystem.h"
#include "vgui\ivgui.h"
#include "ienginevgui.h"
#include "toolframework\ienginetool.h"
#include "inputsystem\iinputsystem.h"
#include "SDK\hl_movedata.h"
#include "SDK\igamemovement.h"
#include "interfaces.hpp"
#include "signals.hpp"
#include "file.hpp"

#if SSDK2007
#include "mathlib\vmatrix.h"
#endif

#include "SPTLib\sptlib.hpp"
#include "tier0\memdbgoff.h" // YaLTeR - switch off the memory debugging.
using namespace std::literals;

namespace interfaces
{
	std::unique_ptr<EngineClientWrapper> engine_client;
	std::unique_ptr<EngineServerWrapper> engine_server;
	IVEngineServer* _engine_server = nullptr;
	IVEngineClient* _engine_client = nullptr;
	std::unique_ptr<SurfaceWrapper> surface;
	IMatSystemSurface* mat_system_surface = nullptr;
	vgui::ISchemeManager* scheme = nullptr;
	vgui::IInput* vgui_input = nullptr;
	IEngineVGui* engine_vgui = nullptr;
	IEngineTool* engine_tool = nullptr;
	IVDebugOverlay* debugOverlay = nullptr;
	IMaterialSystem* materialSystem = nullptr;
	IInputSystem* inputSystem = nullptr;
	IGameMovement* gm = nullptr;
	IClientEntityList* entListClient = nullptr;
	IVModelInfo* modelInfo;
	IBaseClientDLL* clientInterface;
	IEngineTrace* engineTraceClient = nullptr;
	IEngineTrace* engineTraceServer = nullptr;
	IServerPluginHelpers* pluginHelpers = nullptr;
	IPhysicsCollision* physicsCollision = nullptr;
	IStaticPropMgrServer* staticpropmgr = nullptr;
	IShaderDevice* shaderDevice = nullptr;
	ISpatialPartition* spatialPartition = nullptr;
	IServerGameClients* serverGameClients = nullptr;
} // namespace interfaces

ConVar* _viewmodel_fov = nullptr;
ConCommand* _record = nullptr;
ConCommand* _stop = nullptr;
ConVar* _sv_accelerate = nullptr;
ConVar* _sv_airaccelerate = nullptr;
ConVar* _sv_friction = nullptr;
ConVar* _sv_maxspeed = nullptr;
ConVar* _sv_stopspeed = nullptr;
ConVar* _sv_stepsize = nullptr;
ConVar* _sv_gravity = nullptr;
ConVar* _sv_maxvelocity = nullptr;
ConVar* _sv_bounce = nullptr;
ConVar* _sv_cheats = nullptr;

void CallServerCommand(const char* cmd)
{
	if (interfaces::engine_client)
		interfaces::engine_client->ClientCmd(cmd);
}

void GetViewAngles(float viewangles[3])
{
	if (interfaces::engine_client)
	{
		QAngle va;
		interfaces::engine_client->GetViewAngles(va);

		viewangles[0] = va.x;
		viewangles[1] = va.y;
		viewangles[2] = va.z;
	}
}

void SetViewAngles(const float viewangles[3])
{
	if (interfaces::engine_client)
	{
		QAngle va(viewangles[0], viewangles[1], viewangles[2]);
		interfaces::engine_client->SetViewAngles(va);
	}
}

CSourcePauseTool g_SourcePauseTool;
int plugin_interface_version = 0;

extern "C" __declspec(dllexport) const void* CreateInterface(const char* pName, int* pReturnCode)
{
	if (strncmp(pName, "ISERVERPLUGINCALLBACKS00", 24) == 0)
	{
		if (pName[24] >= '1' && pName[24] <= '3' && pName[25] == '\0')
		{
			if (pReturnCode)
				*pReturnCode = 0;

			plugin_interface_version = pName[24] - '0';
			return &g_SourcePauseTool;
		}
	}

	if (pReturnCode)
		*pReturnCode = 0;

	return nullptr;
}

class SDKVersion
{
public:
	enum Value : uint8_t
	{
		SDK_UNKNOWN,
		SDK_2007,
		SDK_2013,
		SDK_BMS,
		SDK_OE,
	};

	SDKVersion() = default;

	constexpr SDKVersion(Value version) : value(version) {}

	constexpr operator Value() const
	{
		return value;
	}

	explicit operator bool() const = delete;

	const char* GetSPTName()
	{
		switch (value)
		{
		case SDK_2007:
			return "spt.dll";
		case SDK_2013:
			return "spt-2013.dll";
		case SDK_BMS:
			return "spt-bms.dll";
		case SDK_OE:
			return "spt-oe.dll";
		default:
			return "";
		}
	}

private:
	Value value;
};

static SDKVersion sptSDKVersion =
#if defined(SSDK2007)
    SDKVersion::SDK_2007
#elif defined(SSDK2013)
    SDKVersion::SDK_2013
#elif defined(BMS)
    SDKVersion::SDK_BMS
#elif defined(OE)
    SDKVersion::SDK_OE
#else
#error "Unknown SDK version"
#endif
    ;

static SDKVersion CheckSDKVersion(CreateInterfaceFn interfaceFactory)
{
	void* icvar = interfaceFactory("VEngineCvar003", NULL);
	if (icvar)
		return SDKVersion::SDK_OE;
	icvar = interfaceFactory("VEngineCvar004", NULL);
	if (!icvar)
		return SDKVersion::SDK_UNKNOWN;

	typedef void*(__thiscall * FindCommandBase_func)(void* thisptr, const char* name);

	// GENIUS HACK (BUT STILL BAD) (from SST): BMS has everything in ICvar shifted
	// down 3 places due to the extra stuff in IAppSystem. This means that
	// if we look up the BMS-specific cvar using FindCommandBase, it
	// *actually* calls the const-overloaded FindVar on other branches,
	// which just happens to still work fine.

	auto find_commandbase_bms = (*(FindCommandBase_func**)(icvar))[13];
	if (find_commandbase_bms(icvar, "kill"))
		return SDKVersion::SDK_BMS;

	if (interfaceFactory("VEngineClient014", NULL))
		return SDKVersion::SDK_2013;

	return SDKVersion::SDK_2007;
}

static void SetFuncIfFound(void** pTarget, void* func, bool critical = false)
{
	if (func)
	{
		*pTarget = func;
	}
	else if (critical)
	{
		DebugBreak();
	}
}

static void GrabTier0Stuff()
{
	HMODULE moduleHandle = GetModuleHandleA("tier0.dll");
	HMODULE moduleHandleVstdlib = GetModuleHandleA("vstdlib.dll");
	if (moduleHandle != NULL)
	{
		SetFuncIfFound((void**)&ConColorMsg, GetProcAddress(moduleHandle, "ConColorMsg"));
		// unmangled versions don't print anything
		SetFuncIfFound((void**)&ConMsg, GetProcAddress(moduleHandle, "?ConMsg@@YAXPBDZZ"));
		SetFuncIfFound((void**)&DevMsg, GetProcAddress(moduleHandle, "?DevMsg@@YAXPBDZZ"));
		SetFuncIfFound((void**)&DevWarning, GetProcAddress(moduleHandle, "?DevWarning@@YAXPBDZZ"));
		SetFuncIfFound((void**)&Msg, GetProcAddress(moduleHandle, "Msg"));
		SetFuncIfFound((void**)&Warning, GetProcAddress(moduleHandle, "Warning"));
		SetFuncIfFound((void**)&_SpewInfo, GetProcAddress(moduleHandle, "_SpewInfo"));
		SetFuncIfFound((void**)&_SpewMessage, GetProcAddress(moduleHandle, "_SpewMessage"));
		SetFuncIfFound((void**)&_ExitOnFatalAssert, GetProcAddress(moduleHandle, "_ExitOnFatalAssert"));
		SetFuncIfFound((void**)&ShouldUseNewAssertDialog,
		               GetProcAddress(moduleHandle, "ShouldUseNewAssertDialog"));
		SetFuncIfFound((void**)&DoNewAssertDialog, GetProcAddress(moduleHandle, "DoNewAssertDialog"));
#if defined(SSDK2013) || defined(BMS)
		SetFuncIfFound((void**)&CallAssertFailedNotifyFunc,
		               GetProcAddress(moduleHandle, "CallAssertFailedNotifyFunc"));
#endif
	}
	if (moduleHandleVstdlib != NULL)
	{
		SetFuncIfFound((void**)&KeyValuesSystem_impl,
		               GetProcAddress(moduleHandleVstdlib, "KeyValuesSystem"),
		               true);
	}
}

bool CSourcePauseTool::Load(CreateInterfaceFn interfaceFactory, CreateInterfaceFn gameServerFactory)
{
	auto startTime = std::chrono::high_resolution_clock::now();
	GrabTier0Stuff();

	SDKVersion detectedVersion = CheckSDKVersion(interfaceFactory);
	if (detectedVersion != sptSDKVersion)
	{
		if (detectedVersion == SDKVersion::SDK_UNKNOWN)
		{
			Warning("This game is not supported by SPT.\n");
		}
		else
		{
			Warning(
			    "You might be using the wrong SPT build (%s)\n"
			    "The correct SPT build seems to be %s\n",
			    sptSDKVersion.GetSPTName(),
			    detectedVersion.GetSPTName());
		}

		skipUnload = true;
		return false;
	}

	if (pluginLoaded)
	{
		Warning("Trying to load SPT when SPT is already loaded.\n");
		// Failure to load causes immediate call to Unload, make sure we do nothing there
		skipUnload = true;
		return false;
	}
	pluginLoaded = true;

	interfaces::gm = (IGameMovement*)gameServerFactory(INTERFACENAME_GAMEMOVEMENT, NULL);
	g_pCVar = (ICvar*)interfaceFactory(CVAR_INTERFACE_VERSION, NULL);
	interfaces::_engine_server = (IVEngineServer*)interfaceFactory(INTERFACEVERSION_VENGINESERVER, NULL);
#ifdef BMS
	interfaces::_engine_client = (IVEngineClient*)interfaceFactory("VEngineClient015", NULL);
#else
	interfaces::_engine_client = (IVEngineClient*)interfaceFactory(VENGINE_CLIENT_INTERFACE_VERSION, NULL);
#endif
	interfaces::debugOverlay = (IVDebugOverlay*)interfaceFactory(VDEBUG_OVERLAY_INTERFACE_VERSION, NULL);
	interfaces::materialSystem = (IMaterialSystem*)interfaceFactory(MATERIAL_SYSTEM_INTERFACE_VERSION, NULL);
	interfaces::engine_vgui = (IEngineVGui*)interfaceFactory(VENGINE_VGUI_VERSION, NULL);
	interfaces::engine_tool = (IEngineTool*)interfaceFactory(VENGINETOOL_INTERFACE_VERSION, NULL);
	interfaces::vgui_input = (vgui::IInput*)interfaceFactory(VGUI_INPUT_INTERFACE_VERSION, NULL);

#ifdef OE
	bool using_mat_system_surface_v4 = false;
	interfaces::mat_system_surface =
	    (IMatSystemSurface*)interfaceFactory(MAT_SYSTEM_SURFACE_INTERFACE_VERSION, NULL);
	if (!interfaces::mat_system_surface)
	{
		interfaces::mat_system_surface =
		    (IMatSystemSurface*)interfaceFactory(MAT_SYSTEM_SURFACE_INTERFACE_VERSION_4, NULL);
		if (interfaces::mat_system_surface)
			using_mat_system_surface_v4 = true;
	}
#else
	interfaces::mat_system_surface =
	    (IMatSystemSurface*)interfaceFactory(MAT_SYSTEM_SURFACE_INTERFACE_VERSION, NULL);
#endif

	interfaces::scheme = (vgui::ISchemeManager*)interfaceFactory(VGUI_SCHEME_INTERFACE_VERSION, NULL);

	interfaces::inputSystem = (IInputSystem*)interfaceFactory(INPUTSYSTEM_INTERFACE_VERSION, NULL);

	auto clientFactory = Sys_GetFactory("client");
	interfaces::entListClient = (IClientEntityList*)clientFactory(VCLIENTENTITYLIST_INTERFACE_VERSION, NULL);
	interfaces::modelInfo = (IVModelInfo*)interfaceFactory(VMODELINFO_SERVER_INTERFACE_VERSION, NULL);
	interfaces::clientInterface = (IBaseClientDLL*)clientFactory(CLIENT_DLL_INTERFACE_VERSION, NULL);
	interfaces::engineTraceClient = (IEngineTrace*)interfaceFactory(INTERFACEVERSION_ENGINETRACE_CLIENT, NULL);
	interfaces::engineTraceServer = (IEngineTrace*)interfaceFactory(INTERFACEVERSION_ENGINETRACE_SERVER, NULL);
	interfaces::pluginHelpers =
	    (IServerPluginHelpers*)interfaceFactory(INTERFACEVERSION_ISERVERPLUGINHELPERS, NULL);
	interfaces::physicsCollision = (IPhysicsCollision*)interfaceFactory(VPHYSICS_COLLISION_INTERFACE_VERSION, NULL);
	interfaces::staticpropmgr =
	    (IStaticPropMgrServer*)interfaceFactory(INTERFACEVERSION_STATICPROPMGR_SERVER, NULL);
	interfaces::shaderDevice = (IShaderDevice*)interfaceFactory(SHADER_DEVICE_INTERFACE_VERSION, NULL);
	interfaces::spatialPartition = (ISpatialPartition*)interfaceFactory(INTERFACEVERSION_SPATIALPARTITION, NULL);
	interfaces::serverGameClients =
	    (IServerGameClients*)gameServerFactory(INTERFACEVERSION_SERVERGAMECLIENTS, NULL);

	if (interfaces::gm)
	{
		DevMsg("SPT: Found IGameMovement at %p.\n", interfaces::gm);
	}
	else
	{
		DevWarning("SPT: Could not find IGameMovement.\n");
		DevWarning("SPT: ProcessMovement logging with spt_tas_log is unavailable.\n");
	}

	if (g_pCVar)
	{
#define GETCVAR(x) _##x = g_pCVar->FindVar(#x);
#ifdef OE
		GETCVAR(viewmodel_fov);
#endif
		GETCVAR(sv_airaccelerate);
		GETCVAR(sv_accelerate);
		GETCVAR(sv_friction);
		GETCVAR(sv_maxspeed);
		GETCVAR(sv_stopspeed);
		GETCVAR(sv_stepsize);
		GETCVAR(sv_gravity);
		GETCVAR(sv_maxvelocity);
		GETCVAR(sv_bounce);
		GETCVAR(sv_cheats);

#ifndef OE
#define GETCMD(x) _##x = g_pCVar->FindCommand(#x);
		GETCMD(record);
		GETCMD(stop);
#endif
	}

#ifdef SSDK2013
	ReplaceAutoCompleteSuggest();
#endif

	if (interfaces::_engine_client)
	{
#ifdef OE
		if (utils::DoesGameLookLikeDMoMM())
			interfaces::engine_client = std::make_unique<IVEngineClientWrapper<IVEngineClientDMoMM>>(
			    reinterpret_cast<IVEngineClientDMoMM*>(interfaces::_engine_client));
#endif
		// Check if we assigned it in the ifdef above.
		if (!interfaces::engine_client)
			interfaces::engine_client =
			    std::make_unique<IVEngineClientWrapper<IVEngineClient>>(interfaces::_engine_client);
	}

	if (!interfaces::engine_client)
		DevWarning("SPT: Failed to get the IVEngineClient interface.\n");

	if (utils::DoesGameLookLikePortal())
	{
		DevMsg("SPT: This game looks like portal. Setting the spt_tas_* cvars appropriately.\n");

		tas_force_airaccelerate.SetValue(15);
		tas_force_wishspeed_cap.SetValue(60);
		tas_reset_surface_friction.SetValue(0);
	}

	if (interfaces::_engine_server)
	{
#ifdef OE
		if (utils::DoesGameLookLikeDMoMM())
			interfaces::engine_server = std::make_unique<IVEngineServerWrapper<IVEngineServerDMoMM>>(
			    reinterpret_cast<IVEngineServerDMoMM*>(interfaces::_engine_server));
#endif
		// Check if we assigned it in the ifdef above.
		if (!interfaces::engine_server)
			interfaces::engine_server =
			    std::make_unique<IVEngineServerWrapper<IVEngineServer>>(interfaces::_engine_server);
	}

	if (!interfaces::engine_server)
		DevWarning("SPT: Failed to get the IVEngineServer interface.\n");

	if (interfaces::mat_system_surface)
	{
#ifdef OE
		if (using_mat_system_surface_v4)
		{
			DevMsg("Using IMatSystemSurface004\n");
			if (utils::DoesGameLookLikeDMoMM())
			{
				interfaces::surface = std::make_unique<ISurfaceWrapper<IMatSystemSurfaceDMoMM>>(
				    reinterpret_cast<IMatSystemSurfaceDMoMM*>(interfaces::mat_system_surface));
			}
			else
			{
				interfaces::surface = std::make_unique<ISurfaceWrapper<IMatSystemSurfaceV4>>(
				    reinterpret_cast<IMatSystemSurfaceV4*>(interfaces::mat_system_surface));
			}
		}
#endif

#ifdef BMS
		if (utils::DoesGameLookLikeBMSLatest())
		{
			interfaces::surface = std::make_unique<ISurfaceWrapper<IMatSystemSurfaceBMSLatest>>(
			    reinterpret_cast<IMatSystemSurfaceBMSLatest*>(interfaces::mat_system_surface));
		}
		else
		{
			interfaces::surface = std::make_unique<ISurfaceWrapper<IMatSystemSurfaceBMS>>(
			    reinterpret_cast<IMatSystemSurfaceBMS*>(interfaces::mat_system_surface));
		}
#endif

#ifdef SSDK2007
		if (plugin_interface_version == 2)
		{
			interfaces::surface = std::make_unique<SurfaceWrapper2007>(interfaces::mat_system_surface);
		}
#endif
		// Check if we assigned it in the ifdef above.
		if (!interfaces::surface)
			interfaces::surface =
			    std::make_unique<ISurfaceWrapper<IMatSystemSurface>>(interfaces::mat_system_surface);
	}

	if (!interfaces::mat_system_surface)
		DevWarning("SPT: Failed to get the IMatSystemSurface interface.\n");

	if (!interfaces::debugOverlay)
		DevWarning("SPT: Failed to get the debug overlay interface.\n");

	if (!interfaces::materialSystem)
		DevWarning("SPT: Failed to get the material system interface.\n");

	if (!interfaces::engine_vgui)
		DevWarning("SPT: Failed to get the engine vgui interface.\n");

	if (!interfaces::engine_tool)
		DevWarning("SPT: Failed to get the engine tool interface.\n");

	if (!interfaces::vgui_input)
		DevWarning("SPT: Failed to get the vgui input interface.\n");

	if (!interfaces::inputSystem)
		DevWarning("SPT: Failed to get the input system interface.\n");

	if (!interfaces::entListClient)
		DevWarning("Unable to retrieve client entity list interface.\n");

	if (!interfaces::modelInfo)
		DevWarning("Unable to retrieve the model info interface.\n");

	if (!interfaces::clientInterface)
		DevWarning("Unable to retrieve the client DLL interface.\n");

	EngineConCmd = CallServerCommand;
	EngineGetViewAngles = GetViewAngles;
	EngineSetViewAngles = SetViewAngles;

	_EngineMsg = Msg;
	_EngineDevMsg = DevMsg;
	_EngineWarning = Warning;
	_EngineDevWarning = DevWarning;

	// Start build number search for build number specific feature stuff
	utils::StartBuildNumberSearch();
	TickSignal.Works = true;
	LevelInitSignal.Works = true;
	ServerActivateSignal.Works = true;
	LevelShutdownSignal.Works = true;
	OnEdictAllocatedSignal.Works = true;
	ClientPutInServerSignal.Works = true;
	ClientDisconnectSignal.Works = true;
	ClientSettingsChangedSignal.Works = true;
	Feature::LoadFeatures();
	Cvar_RegisterSPTCvars();

	auto loadTime =
	    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - startTime)
	        .count();
	std::ostringstream out;
	out << "SourcePauseTool version " << SPT_VERSION << " was loaded in " << loadTime << "ms.\n";

	Msg("%s", std::string(out.str()).c_str());

	return true;
}

extern "C" IMAGE_DOS_HEADER __ImageBase;

void CSourcePauseTool::Unload(void)
{
	if (skipUnload)
	{
		// Preventing double load of plugin, do nothing on unload
		skipUnload = false; // Enable unloading again
		return;
	}

#ifdef SSDK2007
	// Replace the plugin handler with the real handle right before the engine tries to unload.
	// This allows it to actually do so.
	// In newer branches (after 3420) this is redundant but doesn't do any harm.
	auto& plugins = *(CUtlVector<void*>*)((uint32_t)interfaces::pluginHelpers + 4);
	extern CSourcePauseTool g_SourcePauseTool;
	for (int i = 0; i < plugins.Count(); i++)
	{
		// m_pPlugin
		if (*(IServerPluginCallbacks**)((uint32_t)plugins[i] + 132) == &g_SourcePauseTool)
		{
			// m_pPluginModule
			*(void**)((uint32_t)plugins[i] + 140) = ((void*)&__ImageBase);
			break;
		}
	}
#endif

	unloadMutex.lock();
	Cvar_UnregisterSPTCvars();
	Feature::UnloadFeatures();
	unloadMutex.unlock();
	// pray that other threads have left their hooks
	std::this_thread::sleep_for(std::chrono::milliseconds{0});
	Hooks::Free();
	pluginLoaded = false;
}

const char* CSourcePauseTool::GetPluginDescription(void)
{
	extern const char* SPT_DESCRIPTION;
	return SPT_DESCRIPTION;
}

void CSourcePauseTool::GameFrame(bool simulating)
{
	TickSignal(simulating);
}

void CSourcePauseTool::LevelInit(char const* pMapName)
{
	LevelInitSignal(pMapName);
}

void CSourcePauseTool::ServerActivate(edict_t* pEdictList, int edictCount, int clientMax)
{
	ServerActivateSignal(pEdictList, edictCount, clientMax);
}

void CSourcePauseTool::LevelShutdown()
{
	LevelShutdownSignal();
}

#ifndef OE
void CSourcePauseTool::OnEdictAllocated(edict_t* edict)
{
	OnEdictAllocatedSignal(edict);
}
#endif

void CSourcePauseTool::ClientPutInServer(edict_t* pEntity, char const* playername)
{
	ClientPutInServerSignal(pEntity, playername);
}

void CSourcePauseTool::ClientActive(edict_t* pEntity)
{
	ClientActiveSignal(pEntity);
}

void CSourcePauseTool::ClientDisconnect(edict_t* pEntity)
{
	ClientDisconnectSignal(pEntity);
}

void CSourcePauseTool::ClientSettingsChanged(edict_t* pEdict)
{
	ClientSettingsChangedSignal(pEdict);
}

// override new/delete operators to use the game's allocator
// These news and deletes start happening before any of the normal code is ran because of globals, so we gotta always check if we have memalloc
static bool s_hasMemAlloc = false;

static void GetMemAlloc()
{
	if (s_hasMemAlloc)
		return;
	s_hasMemAlloc = true;
	HMODULE moduleHandle = GetModuleHandleA("tier0.dll");
	// We get the address of the pointer via GetProcAddress, so have to dereference it
	g_pMemAlloc = *(IMemAlloc**)GetProcAddress(moduleHandle, "g_pMemAlloc");
}

void* __cdecl operator new(unsigned int nSize)
{
	GetMemAlloc();
	return g_pMemAlloc->Alloc(nSize);
}

void* __cdecl operator new[](unsigned int nSize)
{
	GetMemAlloc();
	return g_pMemAlloc->Alloc(nSize);
}

void* __cdecl operator new(unsigned int nSize, int nBlockUse, const char* pFileName, int nLine)
{
	GetMemAlloc();
	return g_pMemAlloc->Alloc(nSize, pFileName, nLine);
}

void* __cdecl operator new[](unsigned int nSize, int nBlockUse, const char* pFileName, int nLine)
{
	GetMemAlloc();
	return g_pMemAlloc->Alloc(nSize, pFileName, nLine);
}

void __cdecl operator delete(void* pMem)
{
	GetMemAlloc();
	g_pMemAlloc->Free(pMem);
}

void __cdecl operator delete[](void* pMem)
{
	GetMemAlloc();
	g_pMemAlloc->Free(pMem);
}
