#pragma once
#include "vcall.hpp"
#include "cdll_int.h"

class EngineClientWrapper
{
public:
	virtual ~EngineClientWrapper() {};
	virtual void ClientCmd(const char* command) = 0;
	virtual void GetViewAngles(QAngle& viewangles) = 0;
	virtual void SetViewAngles(QAngle& viewangles) = 0;

#ifdef OE
	virtual int Cmd_Argc() = 0;
	virtual const char* Cmd_Argv(int arg) = 0;
#endif
};

#ifdef OE
class IVEngineClientDMoMM
{
public:
	void ClientCmd(const char* command)
	{
		utils::vcall<void>(9, this, command);
	}

	void GetViewAngles(QAngle& viewangles)
	{
		utils::vcall<void>(21, this, &viewangles);
	}

	void SetViewAngles(QAngle& viewangles)
	{
		utils::vcall<void>(22, this, &viewangles);
	}

	int Cmd_Argc()
	{
		return utils::vcall<int>(34, this);
	}

	const char* Cmd_Argv(int arg)
	{
		return utils::vcall<const char*>(35, this, arg);
	}
};
#endif

/**
 * Wrapper for an interface similar to IVEngineClient.
 */
template<class EngineClient>
class IVEngineClientWrapper : public EngineClientWrapper
{
public:
	IVEngineClientWrapper(EngineClient* engine) : engine(engine) {}

	virtual void ClientCmd(const char* command) override
	{
		engine->ClientCmd(command);
	}

	virtual void GetViewAngles(QAngle& viewangles) override
	{
		engine->GetViewAngles(viewangles);
	}

	virtual void SetViewAngles(QAngle& viewangles) override
	{
		engine->SetViewAngles(viewangles);
	}

#ifdef OE
	virtual int Cmd_Argc() override
	{
		return engine->Cmd_Argc();
	}

	virtual const char* Cmd_Argv(int arg) override
	{
		return engine->Cmd_Argv(arg);
	}
#endif

private:
	EngineClient* engine;
};
