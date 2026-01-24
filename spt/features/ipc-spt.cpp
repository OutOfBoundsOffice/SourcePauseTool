#include "stdafx.hpp"
#include "..\feature.hpp"
#include "convar.hpp"
#include "file.hpp"
#include "thirdparty\json_spt_include.hpp"
#include "ent_utils.hpp"
#include "property_getter.hpp"
#include "signals.hpp"
#include "..\scripts\srctas_reader.hpp"
#include "..\ipc\ipc.hpp"
#include "..\sptlib-wrapper.hpp"

namespace ipc
{
	static ipc::IPCServer server;
	void Init();
	void Loop();
	void ShutdownIPC();
	void Send(const nlohmann::json& msg);

	class IPCFeature : public FeatureWrapper<IPCFeature>
	{
	public:
	protected:
		virtual void LoadFeature() override;
		virtual void UnloadFeature() override;

	private:
	};

	static IPCFeature spt_ipc;

	void IPCFeature::UnloadFeature()
	{
		ipc::ShutdownIPC();
	}

#ifdef OE
	void IPC_Changed(ConVar* var, char const* pOldString);
#else
	void IPC_Changed(IConVar* var, const char* pOldValue, float flOldValue);
#endif
	ConVar y_spt_ipc("y_spt_ipc", "0", 0, "Enables IPC.", IPC_Changed);
	ConVar y_spt_ipc_port("y_spt_ipc_port", "27182", 0, "Port used for IPC.");

	void StartIPC()
	{
		if (!ipc::Winsock_Initialized())
		{
			ipc::InitWinsock();
			server.StartListening(y_spt_ipc_port.GetString());
		}
	}

	void ShutdownIPC()
	{
		if (ipc::Winsock_Initialized())
		{
			server.CloseConnections();
			ipc::Shutdown_IPC();
		}
	}

	void CmdCallback(const nlohmann::json& msg)
	{
		nlohmann::json ackMsg;
		ackMsg["type"] = "ack";
		ipc::Send(ackMsg);

		if (msg.find("cmd") != msg.end())
		{
			std::string cmd = msg["cmd"];
			EngineConCmd(cmd.c_str());
		}
		else
		{
			Msg("Message contained no cmd field!\n");
		}
	}

	void MsgWrapper(const char* msg)
	{
		Msg(msg);
	}

	void Init()
	{
		ipc::AddIpcPrintFunc(MsgWrapper);
		if (y_spt_ipc.GetBool())
		{
			StartIPC();
		}
		server.AddCallback("cmd", CmdCallback, false);
	}

	bool IsActive()
	{
		return ipc::Winsock_Initialized() && server.ClientConnected();
	}

	void Loop()
	{
		if (ipc::Winsock_Initialized())
		{
			server.Loop();
		}
	}

	void Send(const nlohmann::json& msg)
	{
		if (ipc::IsActive())
		{
			server.SendMsg(msg);
		}
	}

#ifdef OE
	void IPC_Changed(ConVar* var, char const* pOldString)
#else
	void IPC_Changed(IConVar* var, const char* pOldValue, float flOldValue)
#endif
	{
		if (y_spt_ipc.GetBool())
		{
			StartIPC();
		}
		else
		{
			ipc::ShutdownIPC();
		}
	}

	void MapPropToJson(RecvProp* prop, void* ptr, nlohmann::json& msg)
	{
		void* value = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(ptr) + prop->GetOffset());
		int i;
		float f;
		Vector v;
		Vector2D v2;

		switch (utils::GetPropTypeVersionAdjust(prop->m_RecvType))
		{
		case DPT_Int:
			i = *reinterpret_cast<int*>(value);
			msg[prop->GetName()] = i;
			break;
		case DPT_Float:
			f = *reinterpret_cast<float*>(value);
			msg[prop->GetName()] = f;
			break;
		case DPT_Vector:
			v = *reinterpret_cast<Vector*>(value);
			msg[FormatTempString("%s[0]", prop->GetName())] = v[0];
			msg[FormatTempString("%s[1]", prop->GetName())] = v[1];
			msg[FormatTempString("%s[2]", prop->GetName())] = v[2];
			break;
#ifndef OE
		case DPT_VectorXY:
			v2 = *reinterpret_cast<Vector2D*>(value);
			msg[FormatTempString("%s[0]", prop->GetName())] = v2[0];
			msg[FormatTempString("%s[1]", prop->GetName())] = v2[1];
			break;
		case DPT_String:
			msg[prop->GetName()] = reinterpret_cast<const char*>(value);
			break;
#endif
		default:
			break;
		}
	}

	void MapEntToJson(RecvTable* table, void* ptr, nlohmann::json& json)
	{
		int numProps = table->m_nProps;

		for (int i = 0; i < numProps; ++i)
		{
			auto prop = table->GetProp(i);

			if (strcmp(prop->GetName(), "baseclass") == 0)
			{
				RecvTable* base = prop->GetDataTable();

				if (base)
					MapEntToJson(base, ptr, json);
			}
			else if (prop->GetOffset() != 0)
			{
				MapPropToJson(prop, ptr, json);
			}
		}
	}

	void MapEntToJson(IClientEntity* ent, nlohmann::json& json)
	{
		auto table = ent->GetClientClass()->m_pRecvTable;
		MapEntToJson(table, ent, json);
	}

} // namespace ipc

#if !defined(OE)
CON_COMMAND(y_spt_ipc_ent, "Outputs entity data to IPC client.\n")
{
	if (args.ArgC() < 2)
	{
		Msg("Usage: spt_ipc_ent <entity index>\n");
		return;
	}
	else if (!ipc::IsActive())
	{
		Msg("No IPC client connected.\n");
		return;
	}

	nlohmann::json msg;
	msg["type"] = "ent";
	int index = std::atoi(args.Arg(1));
	auto ent = utils::spt_clientEntList.GetEnt(index);

	if (!ent)
	{
		msg["exists"] = false;
	}
	else
	{
		msg["exists"] = true;
		msg["entity"] = nlohmann::json();
		ipc::MapEntToJson(ent, msg["entity"]);
	}
	ipc::Send(msg);
}
#endif

#if !defined(OE)
CON_COMMAND(y_spt_ipc_properties, "Outputs entity properties to IPC client.\n")
{
	if (args.ArgC() < 2)
	{
		Msg("Usage: spt_ipc_properties <entity index> <property1> <property2> ...\n");
		return;
	}
	else if (!ipc::IsActive())
	{
		Msg("No IPC client connected.\n");
		return;
	}

	nlohmann::json msg;
	msg["type"] = "ent";
	int index = std::atoi(args.Arg(1));
	auto ent = utils::spt_clientEntList.GetEnt(index);

	if (!ent)
	{
		msg["exists"] = false;
	}
	else
	{
		msg["exists"] = true;
		msg["entity"] = nlohmann::json();
		int argc = args.ArgC();
		for (int i = 2; i < argc; ++i)
		{
			const char* propName = args.Arg(i);
			auto prop = spt_propertyGetter.GetRecvProp(index, propName);

			if (prop)
			{
				ipc::MapPropToJson(prop, ent, msg["entity"]);
			}
			else
			{
				Msg("Could not find prop %s\n", propName);
			}
		}
	}
	ipc::Send(msg);
}
#endif

CON_COMMAND(y_spt_ipc_echo, "Sends a text message to IPC client. Don't insert fancy characters into this.\n")
{
	if (args.ArgC() < 2)
	{
		Msg("Usage: spt_ipc_echo <text>\n");
		return;
	}
	else if (!ipc::IsActive())
	{
		Msg("No IPC client connected.\n");
		return;
	}

	int argc = args.ArgC();
	nlohmann::json msg;

	std::string text;
#if !defined(OE)
	auto length = std::strlen(args.ArgS());
	text.reserve(length);
#endif
	msg["type"] = "echo";

	for (int i = 1; i < argc; i++)
	{
		if (i > 1)
		{
			text.push_back(' ');
		}
		text += args.Arg(i);
	}

	try
	{
		msg["text"] = text;
		ipc::Send(msg);
	}
	catch (const std::exception& ex)
	{
		Msg("Error sending message: %s\n", ex.what());
	}
}

CON_COMMAND(y_spt_ipc_playback, "Outputs the current playback tick to IPC client.\n")
{
	if (!ipc::IsActive())
	{
		Msg("No IPC client connected.\n");
		return;
	}
	nlohmann::json msg;
	msg["type"] = "playback";
	msg["tick"] = scripts::g_TASReader.GetCurrentTick();
	msg["length"] = scripts::g_TASReader.GetCurrentScriptLength();
	ipc::Send(msg);
}

CON_COMMAND(y_spt_ipc_gamedir, "Output the game directory path to IPC client.\n")
{
	if (!ipc::IsActive())
	{
		Msg("No IPC client connected.\n");
		return;
	}

	nlohmann::json msg;
	msg["type"] = "gamedir";
	msg["path"] = GetGameDir();
	ipc::Send(msg);
}

void ipc::IPCFeature::LoadFeature()
{
	if (FrameSignal.Works)
	{
		Init();
		FrameSignal.Connect(Loop);
#ifndef OE
		InitCommand(y_spt_ipc_ent);
		InitCommand(y_spt_ipc_properties);
#endif
		InitCommand(y_spt_ipc_echo);
		InitCommand(y_spt_ipc_playback);
		InitCommand(y_spt_ipc_gamedir);

		InitConcommandBase(y_spt_ipc);
		InitConcommandBase(y_spt_ipc_port);
	}
}
