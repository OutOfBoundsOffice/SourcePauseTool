#include "stdafx.hpp"
#include "..\feature.hpp"
#include "file.hpp"
#include "sptlib-wrapper.hpp"
#include "signals.hpp"


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


enum luacallbacks {
    on_tick,
    on_frame,
    num_lua_callbacks
};

static const char* callback_func_names[num_lua_callbacks] = {};

static int callback_funs[num_lua_callbacks];

static void init_lua_callbacks()
{
	for (size_t i = 0; i < num_lua_callbacks; ++i)
	{
		callback_funs[i] = LUA_NOREF;
	}

	if (TickSignal.Works)
	{ 
		callback_func_names[on_tick] = "on_tick";
	}

	if (FrameSignal.Works)
	{
		callback_func_names[on_frame] = "on_frame";
	}
}

static enum luacallbacks get_callback(lua_State* L, const char* string)
{
    for (size_t i = 0; i < num_lua_callbacks; ++i)
    {
        if (strcmp(string, callback_func_names[i]) == 0)
        {
            return (enum luacallbacks)i;
        }
    }

    luaL_error(L, "callback %s doesn't exist", string);
    return num_lua_callbacks;
}

static int register_hook(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    size_t len;
    const char* func_name = luaL_checklstring(L, 2, &len);

    enum luacallbacks cb = get_callback(L, func_name);

    if (callback_funs[(size_t)cb] != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, callback_funs[(size_t)cb]);
    }

    /* Push the function to the top of the stack (it's already at index 1,
        but luaL_ref pops from the top) */
    lua_pushvalue(L, 1);

    /* Store it in the registry and save the reference */
    callback_funs[(size_t)cb] = luaL_ref(L, LUA_REGISTRYINDEX);

    return 0;
}

static int lua_concommand(lua_State* L) {
	size_t len;
    const char* command = luaL_checklstring(L, 1, &len);
	EngineConCmd(command);

    return 0;
}

static void fire_callback(lua_State* L, enum luacallbacks cb)
{
    if (callback_funs[(size_t)cb] == LUA_NOREF) {
        return; /* no callback registered */
    }

    /* Push the function from the registry onto the stack */
    lua_rawgeti(L, LUA_REGISTRYINDEX, callback_funs[(size_t)cb]);

    /* Call: 1 argument, 0 return values, no error handler */
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        fprintf(stderr, "Lua callback error: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1); /* pop error message */
    }
}

static const luaL_Reg lua_spt_lib[] = {
    {"hook", register_hook},
	{"console", lua_concommand},
    {NULL, NULL}
};

LUAMOD_API int luaopen_spt(lua_State* L) {
    luaL_newlib(L, lua_spt_lib);
	init_lua_callbacks();

    return 1;
}

/*
** Check whether 'status' is not OK and, if so, prints the error
** message on the top of the stack.
*/
static int report(lua_State* L, int status) {
	if (status != LUA_OK) {
		const char* msg = lua_tostring(L, -1);
		if (msg == NULL)
			msg = "(error message not a string)";
		Warning("lua error: %s\n", msg);
		lua_pop(L, 1);  /* remove message */
	}
	return status;
}


static int dochunk(lua_State* L, int status) {
	if (status == LUA_OK) status = docall(L, 0, 0);
	return report(L, status);
}


static int dofile(lua_State* L, const char* name) {
	return dochunk(L, luaL_loadfile(L, name));
}


static int dostring(lua_State* L, const char* s, const char* name) {
	return dochunk(L, luaL_loadbuffer(L, s, strlen(s), name));
}


CON_COMMAND_AUTOCOMPLETEFILE(spt_lua_script, "Run .lua script", FCVAR_CHEAT, "lua", ".lua")
{
	if (args.ArgC() == 1)
	{
		Msg("Usage: spt_lua_script lua/your_script.lua\n");
		return;
	}

	std::string path = GetGameDir() + "/lua/" + args.Arg(1) + ".lua";
	dofile(lua_feature.L, path.c_str());
}

CON_COMMAND_F(spt_lua_run, "Run inline script", FCVAR_CHEAT)
{
	if (args.ArgC() == 1)
	{
		Msg("Usage: spt_lua_run <inline script>\n");
		return;
	}

#ifndef OE
	dostring(lua_feature.L, args.ArgS(), "");
#else
	dostring(lua_feature.L, args.Arg(1), "");
#endif
}

static void init_spt_lua()
{
	lua_feature.L = luaL_newstate();
	luaL_openselectedlibs(lua_feature.L, ~0, 0);
	luaL_requiref(lua_feature.L, "spt", luaopen_spt, 1);
	lua_gc(lua_feature.L, LUA_GCRESTART);  /* start GC... */
	lua_gc(lua_feature.L, LUA_GCGEN);  /* ...in generational mode */
}

CON_COMMAND_F(spt_lua_reset, "Reset lua state", FCVAR_CHEAT)
{
	lua_close(lua_feature.L);
	init_spt_lua();
}

bool LuaFeature::ShouldLoadFeature()
{
	return true;
}

void LuaFeature::InitHooks() {}

static void on_frame_hook() { fire_callback(lua_feature.L, on_frame);  }
static void on_tick_hook(bool simulating) { fire_callback(lua_feature.L, on_tick); }


void LuaFeature::LoadFeature() 
{
	InitCommand(spt_lua_script);
	InitCommand(spt_lua_run);
	InitCommand(spt_lua_reset);

	init_spt_lua();

	FrameSignal.Connect(on_frame_hook);
	TickSignal.Connect(on_tick_hook);
}

void LuaFeature::UnloadFeature() 
{
	lua_close(L);
}
