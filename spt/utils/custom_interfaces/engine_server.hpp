#pragma once
#include "vcall.hpp"
#include "eiface.h"

class EngineServerWrapper
{
public:
	virtual ~EngineServerWrapper() {};
	virtual int GetEntityCount() = 0;
	virtual edict_t* PEntityOfEntIndex(int iEntIndex) = 0;
	virtual void GetGameDir(char* szGetGameDir, int maxlength) = 0;
	virtual const char* GetClientConVarValue(int clientIndex, const char* name) = 0;
};

#ifdef OE
class IVEngineServerDMoMM
{
public:
	int GetEntityCount()
	{
		return utils::vcall<int>(21, this);
	}

	edict_t* PEntityOfEntIndex(int iEntIndex)
	{
		return utils::vcall<edict_t*>(23, this, iEntIndex);
	}

	void GetGameDir(char* szGetGameDir, int maxlength) 
	{
		utils::vcall<void>(59, this, szGetGameDir, maxlength);
	}

	const char* GetClientConVarValue(int clientIndex, const char* name)
	{
		return utils::vcall<const char*>(63, this, clientIndex, name);
	}
};
#endif

/**
 * Wrapper for an interface similar to EngineServerWrapper.
 */
template<class EngineServer>
class IVEngineServerWrapper : public EngineServerWrapper
{
public:
	IVEngineServerWrapper(EngineServer* engine) : engine(engine) {}

	virtual int GetEntityCount() override
	{
		return engine->GetEntityCount();
	}

	virtual edict_t* PEntityOfEntIndex(int iEntIndex) override
	{
		return engine->PEntityOfEntIndex(iEntIndex);
	}

	virtual void GetGameDir(char* szGetGameDir, int maxlength) override
	{
		engine->GetGameDir(szGetGameDir, maxlength);
	}

	virtual const char* GetClientConVarValue(int clientIndex, const char* name) override
	{
		return engine->GetClientConVarValue(clientIndex, name);
	}

private:
	EngineServer* engine;
};
