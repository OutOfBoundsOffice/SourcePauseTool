#include "stdafx.hpp"
#include "..\feature.hpp"
#include "file.hpp"


#define lua_c
extern "C"
{
#include "lua/lprefix.h"
#include "lua/lua.h"
#include "lua/lauxlib.h"
#include "lua/lualib.h"
#include "lua/llimits.h"
}

// Feature description
class LuaFeature : public FeatureWrapper<LuaFeature>
{
public:
	lua_State* L;
protected:

	virtual bool ShouldLoadFeature() override;

	virtual void InitHooks() override;

	virtual void LoadFeature() override;

	virtual void UnloadFeature() override;
};

static LuaFeature lua_feature;

void lua_writestring(const char* str, size_t num_bytes)
{
	Msg("%.*s", num_bytes, str);
}

void lua_writestringerror(const char* fmt, const char* err)
{
	Warning(fmt, err);
}


/*
** Message handler used to run all chunks
*/
static int msghandler(lua_State* L) {
	const char* msg = lua_tostring(L, 1);
	if (msg == NULL) {  /* is error object not a string? */
		if (luaL_callmeta(L, 1, "__tostring") &&  /* does it have a metamethod */
			lua_type(L, -1) == LUA_TSTRING)  /* that produces a string? */
			return 1;  /* that is the message */
		else
			msg = lua_pushfstring(L, "(error object is a %s value)",
				luaL_typename(L, 1));
	}
	luaL_traceback(L, L, msg, 1);  /* append a standard traceback */
	return 1;  /* return the traceback */
}

static int docall(lua_State* L, int narg, int nres) {
	int status;
	int base = lua_gettop(L) - narg;  /* function index */
	lua_pushcfunction(L, msghandler);  /* push message handler */
	lua_insert(L, base);  /* put it under function and args */
	status = lua_pcall(L, narg, nres, base);
	lua_remove(L, base);  /* remove message handler from the stack */
	return status;
}

CON_COMMAND_AUTOCOMPLETEFILE(spt_lua_script, "Run .lua script", FCVAR_CHEAT, "lua", ".lua")
{
	if (args.ArgC() == 1)
	{
		Msg("Usage: spt_lua_script lua/your_script.lua\n");
		return;
	}

	std::string path = GetGameDir() + "/lua/" + args.Arg(1) + ".lua";
	int status = luaL_loadfile(lua_feature.L, path.c_str());

	if (status != LUA_OK)
	{
		Warning("Running script %s failed with error code %d\n", args.Arg(1), status);
		return;
	}

	docall(lua_feature.L, 0, 0);
}


bool LuaFeature::ShouldLoadFeature()
{
	return true;
}

void LuaFeature::InitHooks() {}

void LuaFeature::LoadFeature() 
{
	InitCommand(spt_lua_script);
	L = luaL_newstate();
	luaL_openselectedlibs(L, ~0, 0);
	lua_gc(L, LUA_GCRESTART);  /* start GC... */
	lua_gc(L, LUA_GCGEN);  /* ...in generational mode */
}

void LuaFeature::UnloadFeature() 
{
	lua_close(L);
}
