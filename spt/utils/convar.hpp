#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "convar.h"
#ifdef OE
#include "interfaces.hpp"

#define FCVAR_HIDDEN FCVAR_ARCHIVE_XBOX // steal an xbox flag because OE doesn't have a hidden flag
#endif
#define FCVAR_TAS_RESET (1 << 31)

struct ConCommandBase_guts
{
	void** vfptr;
	ConCommandBase* m_pNext;
	bool m_bRegistered;

	// Static data
	const char* m_pszName;
	const char* m_pszHelpString;

	// ConVar flags
	int m_nFlags;
};

struct ConVar_guts : public ConCommandBase_guts
{
#ifndef OE
	void** vfptr_iconvar;
#endif
	ConVar* m_pParent;

	// Static data
	const char* m_pszDefaultValue;

	// Value
	// Dynamically allocated
	char* m_pszString;
	int m_StringLength;

	// Values
	float m_fValue;
	int m_nValue;

	// Min/Max values
	bool m_bHasMin;
	float m_fMinVal;
	bool m_bHasMax;
	float m_fMaxVal;

	// Call this function when ConVar changes
#ifndef OE
	FnChangeCallback_t m_fnChangeCallback;
#else
	typedef void (*FnChangeCallback)(ConVar* var, char const* pOldString);
	FnChangeCallback m_fnChangeCallback;
#endif
};

class ConVarProxy : public ConVar
{
public:
	ConVarProxy(ConVar* parent, const char* pName, int flags = 0) : ConVar(pName, 0, flags)
	{
		reinterpret_cast<ConVar_guts*>(this)->m_pParent = parent;
	}

	// Allow proxies to have different flags from it's parent (i.e. be hidden while the parent is not)
	virtual bool IsFlagSet(int flag) const
	{
		return ConCommandBase::IsFlagSet(flag);
	};

	virtual void AddFlags(int flag)
	{
		return ConCommandBase::AddFlags(flag);
	}

	// This allows this cvar to be registered independently from it's parent
	virtual bool IsRegistered(void) const
	{
		return ConCommandBase::IsRegistered();
	}

	// This allows this cvar to be distinguished from it's parent
	virtual const char* GetName(void) const
	{
		return ConCommandBase::GetName();
	};
};

#ifndef SSDK2007
typedef void (*FnCommandCallbackV1_t)(void);
#endif

struct ConCommand_guts : public ConCommandBase_guts
{
#ifndef OE
	union
	{
		FnCommandCallbackV1_t m_fnCommandCallbackV1;
		FnCommandCallback_t m_fnCommandCallback;
		ICommandCallback* m_pCommandCallback;
	};

	union
	{
		FnCommandCompletionCallback m_fnCompletionCallback;
		ICommandCompletionCallback* m_pCommandCompletionCallback;
	};

	bool m_bHasCompletionCallback : 1;
	bool m_bUsingNewCommandCallback : 1;
	bool m_bUsingCommandCallbackInterface : 1;
#else
	FnCommandCallbackV1_t m_fnCommandCallback;
	FnCommandCompletionCallback m_fnCompletionCallback;
	bool m_bHasCompletionCallback;
#endif
};

#define AUTOCOMPLETION_FUNCTION_PARAMS \
	const char *partial, char commands[COMMAND_COMPLETION_MAXITEMS][COMMAND_COMPLETION_ITEM_LENGTH]

class AutoCompleteList
{
public:
	AutoCompleteList();
	AutoCompleteList(std::vector<std::string> completions);
	virtual int AutoCompletionFunc(AUTOCOMPLETION_FUNCTION_PARAMS);
	static int AutoCompleteSuggest(const std::string& suggestionBase,
	                               const std::string& incompleteArgument,
	                               const std::vector<std::string>& _completions,
	                               bool hasDoubleQuote,
	                               char commands[COMMAND_COMPLETION_MAXITEMS][COMMAND_COMPLETION_ITEM_LENGTH]);

protected:
	std::pair<std::string, std::string> SplitPartial(const char* partial, bool& hasDoubleQuote);
	std::vector<std::string> completions;
};

class FileAutoCompleteList : public AutoCompleteList
{
public:
	FileAutoCompleteList(const char* subDirectory, const char* extension);
	int AutoCompletionFunc(AUTOCOMPLETION_FUNCTION_PARAMS);

private:
	const char* subDirectory;
	std::string extension;
	std::filesystem::path prevPath;
};

#ifdef OE
#undef CON_COMMAND
#undef CON_COMMAND_F

struct ArgsWrapper
{
	ArgsWrapper() {};

	int ArgC() const
	{
		return interfaces::engine_client->Cmd_Argc();
	};

	const char* Arg(int arg) const
	{
		return interfaces::engine_client->Cmd_Argv(arg);
	};
};

#define _CON_COMMAND_WRAPPER(name) \
	static void name(ArgsWrapper args); \
	static void name##_wrapper() \
	{ \
		if (!interfaces::engine_client) \
			return; \
		ArgsWrapper args; \
		name(args); \
	}

#define CON_COMMAND(name, description) \
	_CON_COMMAND_WRAPPER(name) \
	static ConCommand name##_command(#name, name##_wrapper, description); \
	static void name(ArgsWrapper args)

#define CON_COMMAND_F(name, description, flags) \
	_CON_COMMAND_WRAPPER(name) \
	static ConCommand name##_command(#name, name##_wrapper, description, flags); \
	static void name(ArgsWrapper args)

#define CON_COMMAND_F_COMPLETION(name, description, flags, completion) \
	_CON_COMMAND_WRAPPER(name); \
	static ConCommand name##_command(#name, name##_wrapper, description, flags, completion); \
	static void name(ArgsWrapper args)

#define CON_COMMAND_DOWN(name, description) \
	_CON_COMMAND_WRAPPER(name##_down); \
	static ConCommand name##_down##_command("+" #name, name##_down##_wrapper, description); \
	static void name##_down(ArgsWrapper args)

#define CON_COMMAND_UP(name, description) \
	_CON_COMMAND_WRAPPER(name##_up); \
	static ConCommand name##_up##_command("-" #name, name##_up##_wrapper, description); \
	static void name##_up(ArgsWrapper args)
#else
#define CON_COMMAND_DOWN(name, description) \
	static void name##_down(const CCommand& args); \
	static ConCommand name##_down##_command("+" #name, name##_down, description); \
	static void name##_down(const CCommand& args)

#define CON_COMMAND_UP(name, description) \
	static void name##_up(const CCommand& args); \
	static ConCommand name##_up##_command("-" #name, name##_up, description); \
	static void name##_up(const CCommand& args)
#endif

#define AUTOCOMPLETION_FUNCTION(command) command##_CompletionFunc

#define DEFINE_AUTOCOMPLETIONFILE_FUNCTION(command, subdirectory, extension) \
	static FileAutoCompleteList command##Complete(subdirectory, extension); \
	static int AUTOCOMPLETION_FUNCTION(command)(AUTOCOMPLETION_FUNCTION_PARAMS) \
	{ \
		return command##Complete.AutoCompletionFunc(partial, commands); \
	}

#define CON_COMMAND_AUTOCOMPLETEFILE(name, description, flags, subdirectory, extension) \
	DEFINE_AUTOCOMPLETIONFILE_FUNCTION(name, subdirectory, extension) \
	CON_COMMAND_F_COMPLETION(name, description, flags, AUTOCOMPLETION_FUNCTION(name))

#define DEFINE_AUTOCOMPLETION_FUNCTION(command, completion) \
	static AutoCompleteList command##Complete(std::vector<std::string> completion); \
	static int AUTOCOMPLETION_FUNCTION(command)(AUTOCOMPLETION_FUNCTION_PARAMS) \
	{ \
		return command##Complete.AutoCompletionFunc(partial, commands); \
	}

#define CON_COMMAND_AUTOCOMPLETE(name, description, flags, completion) \
	DEFINE_AUTOCOMPLETION_FUNCTION(name, completion) \
	CON_COMMAND_F_COMPLETION(name, description, flags, AUTOCOMPLETION_FUNCTION(name))

#ifdef OE
#define CON_COMMAND_CALLBACK_ARGS ConVar *var, char const *pOldString
#else
#define CON_COMMAND_CALLBACK_ARGS IConVar *var, const char *pOldValue, float flOldValue
#endif

#ifdef OE
ConCommand* FindCommand(const char* name);
#endif
