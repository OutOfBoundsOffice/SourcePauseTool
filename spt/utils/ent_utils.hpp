#pragma once

#include <string>
#include "cdll_int.h"
#include "client_class.h"
#include "engine\ivmodelinfo.h"
#include "icliententity.h"
#include "icliententitylist.h"
#include "iserverunknown.h"
#include "trace.h"

#define INDEX_MASK (MAX_EDICTS - 1)

namespace utils
{
	struct propValue
	{
		std::string name;
		std::string value;
		RecvProp* prop;

		int GetOffset();
		propValue(const char* name, const char* value, RecvProp* prop) : name(name), value(value), prop(prop) {}
	};

	// may need to adjust for different games since RecvProp::m_RecvType changed between versions
	SendPropType GetPropTypeVersionAdjust(SendPropType original);
	void GetAllProps(RecvTable* table, void* ptr, std::vector<propValue>& props);
	void PrintAllProps(int index);
	Vector GetPlayerEyePosition();
	QAngle GetPlayerEyeAngles();
	int FillInfoArray(std::string argString, wchar* arr, int maxEntries, int bufferSize, char sep, char entSep);
	void SimulateFrames(int frames);
	struct JBData
	{
		bool canJB;
		float landingHeight;
		int ticks;
	};

	JBData CanJB(float height);
	bool GetPunchAngleInformation(QAngle& punchAngle, QAngle& punchAngleVel);
} // namespace utils
