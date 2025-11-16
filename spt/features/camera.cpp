#include "stdafx.hpp"

#ifndef OE

#include "spt\feature.hpp"
#include "spt\features\demo.hpp"
#include "spt\features\playerio.hpp"
#include "spt\features\overlay.hpp"
#include "spt\features\generic.hpp"
#include "spt\utils\convar.hpp"
#include "spt\utils\ent_utils.hpp"
#include "spt\utils\interfaces.hpp"
#include "spt\utils\signals.hpp"
#include "spt\cvars.hpp"
#include "spt\sptlib-wrapper.hpp"
#include "visualizations\player_trace\tr_config.hpp"

#include "usercmd.h"
#include "view_shared.h"

#include <chrono>
#include <sstream>
#include <map>

#define CAM_FORWARD (1 << 0)
#define CAM_BACK (1 << 1)
#define CAM_MOVELEFT (1 << 2)
#define CAM_MOVERIGHT (1 << 3)
#define CAM_MOVEUP (1 << 4)
#define CAM_MOVEDOWN (1 << 5)
#define CAM_DUCK (1 << 6)
#define CAM_SPEED (1 << 7)

// Camera stuff
class Camera : public FeatureWrapper<Camera>
{
public:
	enum CameraInfoParameter
	{
		ORIGIN_X,
		ORIGIN_Y,
		ORIGIN_Z,
		ANGLES_X,
		ANGLES_Y,
		ANGLES_Z,
		FOV
	};

	struct DriveEntInfo
	{
		int entIndex = 0;
		Vector posOffsets = Vector();
		QAngle angOffsets = QAngle();
	};

	struct CameraInfo
	{
		Vector origin = Vector();
		QAngle angles = QAngle();
		float fov = 75.0f;
		operator std::string() const
		{
			std::ostringstream s;
			s << "pos: " << origin.x << " " << origin.y << " " << origin.z << '\n';
			s << "ang: " << angles.x << " " << angles.y << " " << angles.z << '\n';
			s << "fov: " << fov;
			return s.str();
		}
	};

	// Original player camera
	CameraInfo playerCam;
	// Player camera after override
	CameraInfo currentCam;

	DriveEntInfo driveEntInfo;
	std::map<int, CameraInfo> keyframes;

	bool CanOverrideView() const;
	bool CanInput() const;
	void RecomputeInterpPath();

protected:
	virtual bool ShouldLoadFeature() override;
	virtual void InitHooks() override;
	virtual void PreHook() override;
	virtual void LoadFeature() override;
	virtual void UnloadFeature() override;

private:
	DECL_HOOK_THISCALL(void, ClientModeShared__OverrideView, void*, CViewSetup* viewSetup);
	DECL_HOOK_THISCALL(bool, C_BasePlayer__ShouldDrawLocalPlayer, void*);
	DECL_HOOK_THISCALL(void, CInput__MouseMove, void*, void* cmd);
	DECL_HOOK_THISCALL(void,
	                   CInputSystem__PostButtonPressedEvent,
	                   void*,
	                   InputEventType_t nType,
	                   int nTick,
	                   ButtonCode_t scanCode,
	                   ButtonCode_t virtualCode);
	DECL_HOOK_THISCALL(void,
	                   CInputSystem__PostButtonReleasedEvent,
	                   void*,
	                   InputEventType_t nType,
	                   int nTick,
	                   ButtonCode_t scanCode,
	                   ButtonCode_t virtualCode);
#if defined(SSDK2013)
	DECL_HOOK_THISCALL(bool, C_BasePlayer__ShouldDrawThisPlayer, void*);
#endif
	bool ShouldDrawPlayerModel();
	void OverrideView(CViewSetup* viewSetup);
	void HandleDriveMode(bool active);
	void HandleInput(bool active);
	void HandleDriveEntityMode(bool active);
	bool ProcessInputKey(ButtonCode_t keyCode, bool state);
	void HandleCinematicMode(bool active);
	void HandleTraceMode(bool active);
	static std::vector<Vector> CameraInfoToPoints(float* x, CameraInfo* y, CameraInfoParameter param);
	CameraInfo InterpPath(float time);
	void RequestTimeOffsetRefresh();

#ifdef SPT_MESH_RENDERING_ENABLED
	void OnMeshRenderSignal(MeshRendererDelegate& mr);
	StaticMesh interpPathMesh;
	std::vector<CameraInfo> interpPathCache;
#endif

	bool loadingSuccessful = false;

	bool timeOffsetRefreshRequested = true;
	bool driveEntExists = false;
	int screenHeight = 0;
	int screenWidth = 0;
	int oldCursor[2] = {0, 0};
	float timeOffset = 0.0f;
	uint32_t keyBits = 0;

	ConVar* sensitivity = nullptr;
};

Camera spt_camera;

ConVar y_spt_cam_control(
    "y_spt_cam_control",
    "0",
    FCVAR_CHEAT,
    "Changes the camera control type.\n"
    "0 = Default\n"
    "1 = Drive mode\n"
    "    Camera is separated and can be controlled by user input.\n"
    "    Use +forward, +back, +moveright, +moveleft, +moveup, +movedown, +duck, +speed keys to drive camera.\n"
    "    In-game: Set spt_cam_drive 1 to enable drive mode.\n"
    "    Demo playback: Hold right mouse button to enable drive mode.\n"
    "2 = Cinematic mode\n"
    "    Camera is controlled by predefined path.\n"
    "    See commands spt_cam_path_\n"
    "3 = Entity drive mode\n"
    "    Sets the camera position to an entity. Use spt_cam_drive_ent to set the entity.\n"
    "4 = Trace drive mode\n"
    "    Sets the camera position to the currently active trace");
ConVar y_spt_cam_drive("y_spt_cam_drive", "1", FCVAR_CHEAT, "Enables or disables camera drive mode in-game.");
ConVar y_spt_cam_drive_speed("y_spt_cam_drive_speed", "200", 0, "Speed for moving in camera drive mode.");
ConVar y_spt_cam_path_interp("y_spt_cam_path_interp",
                             "1",
                             0,
                             "Sets interpolation type between keyframes for cinematic camera.\n"
                             "0 = Linear interpolation\n"
                             "1 = Cubic spline\n"
                             "2 = Piecewise Cubic Hermite Interpolating Polynomial (PCHIP)");
ConVar y_spt_cam_path_draw("y_spt_cam_path_draw", "0", FCVAR_CHEAT, "Draws the current camera path.");

ConVar _y_spt_force_fov("_y_spt_force_fov", "0", 0, "Force FOV to some value.");

ConVar spt_cam_draw_player("spt_cam_draw_player", "1", 0, "Draws player model when camera control enabled.");

// black mesa broke cl_mousenable and cl_mouselook so we'll need this...
ConVar y_spt_mousemove(
    "y_spt_mousemove",
    "1",
    0,
    "Enables or disables reacting to mouse movement (for view angle changing or for movement using +strafe).");

extern ConVar _y_spt_overlay;
extern ConVar _y_spt_overlay_swap;

CON_COMMAND(y_spt_cam_setpos, "Sets the camera position. (requires camera drive mode)")
{
	if (args.ArgC() != 4)
	{
		Msg("Usage: %s <x> <y> <z>\n", args.Arg(0));
		return;
	}
	if (y_spt_cam_control.GetInt() != 1 || !spt_camera.CanOverrideView())
	{
		Msg("Requires camera drive mode.\n");
		return;
	}
	Vector pos = {0.0, 0.0, 0.0};
	for (int i = 0; i < args.ArgC() - 1; i++)
	{
		pos[i] = atof(args.Arg(i + 1));
	}
	spt_camera.currentCam.origin = pos;
}

CON_COMMAND(y_spt_cam_setang, "Sets the camera angles. (requires camera drive mode)")
{
	if (args.ArgC() != 3 && args.ArgC() != 4)
	{
		Msg("Usage: %s <pitch> <yaw> [roll]\n", args.Arg(0));
		return;
	}
	if (y_spt_cam_control.GetInt() != 1 || !spt_camera.CanOverrideView())
	{
		Msg("Requires camera drive mode.\n");
		return;
	}
	QAngle ang = {0.0, 0.0, 0.0};
	for (int i = 0; i < args.ArgC() - 1; i++)
	{
		ang[i] = atof(args.Arg(i + 1));
	}
	spt_camera.currentCam.angles = ang;
}

CON_COMMAND(y_spt_cam_path_setkf, "Sets a camera path keyframe.")
{
	int argc = args.ArgC();
	Camera::CameraInfo info;
	int tick;
	if (argc == 1)
	{
		if (!spt_demostuff.Demo_IsPlayingBack())
		{
			Msg("Set keyframe with no arguments requires demo playback.\n");
			return;
		}
		tick = spt_demostuff.Demo_GetPlaybackTick();

		switch (y_spt_cam_control.GetInt())
		{
		case 1:
			info = spt_camera.currentCam;
			break;
		case 2:
			Msg("Cannot set keyframe in Cinematic mode.");
			return;
		default:
			info = spt_camera.currentCam;
		}
	}
	else if (argc == 9)
	{
		tick = atoi(args.Arg(1));
		float nums[7] = {0};
		for (int i = 0; i < args.ArgC() - 2; i++)
		{
			nums[i] = std::stof(args.Arg(i + 2));
		}
		info.origin.x = nums[0];
		info.origin.y = nums[1];
		info.origin.z = nums[2];
		info.angles.x = nums[3];
		info.angles.y = nums[4];
		info.angles.z = nums[5];
		info.fov = nums[6];
	}
	else
	{
		Msg("Usage: %s [frame] [x] [y] [z] [yaw] [pitch] [roll] [fov]\n", args.Arg(0));
		return;
	}
	spt_camera.keyframes[tick] = info;
	Msg("[%d]\n%s\n", tick, std::string(info).c_str());
	spt_camera.RecomputeInterpPath();
}

CON_COMMAND(y_spt_cam_path_showkfs, "Prints all keyframes.")
{
	for (const auto& kv : spt_camera.keyframes)
	{
		Msg("[%d]\n%s\n", kv.first, std::string(kv.second).c_str());
	}
}

CON_COMMAND(y_spt_cam_path_getkfs, "Prints all keyframes in commands.")
{
	for (const auto& kv : spt_camera.keyframes)
	{
		Camera::CameraInfo info = kv.second;
		Msg("spt_cam_path_setkf %d %f %f %f %f %f %f %f;\n",
		    kv.first,
		    info.origin.x,
		    info.origin.y,
		    info.origin.z,
		    info.angles.x,
		    info.angles.y,
		    info.angles.z,
		    info.fov);
	}
}

static int y_spt_cam_path_rmkf_CompletionFunc(AUTOCOMPLETION_FUNCTION_PARAMS)
{
	std::vector<std::string> completion;
	for (const auto& kv : spt_camera.keyframes)
	{
		completion.push_back(std::to_string(kv.first));
	}
	AutoCompleteList y_spt_cam_path_rmkf_Complete(completion);
	return y_spt_cam_path_rmkf_Complete.AutoCompletionFunc(partial, commands);
}

CON_COMMAND_F_COMPLETION(y_spt_cam_path_rmkf, "Removes a keyframe.", 0, y_spt_cam_path_rmkf_CompletionFunc)
{
	if (args.ArgC() != 2)
	{
		Msg("Usage: %s <tick>\n", args.Arg(0));
		return;
	}

	int tick = atoi(args.Arg(1));
	bool removed = spt_camera.keyframes.erase(tick);
	if (removed)
	{
		spt_camera.RecomputeInterpPath();
		Msg("Removed keyframe at frame %d.\n", tick);
	}
	else
		Msg("Keyframe at frame %d does not exist.\n", tick);
}

CON_COMMAND(y_spt_cam_path_clear, "Removes all keyframes.")
{
	spt_camera.keyframes.clear();
	spt_camera.RecomputeInterpPath();
	Msg("Removed all keyframes.\n");
}

CON_COMMAND(y_spt_cam_drive_ent, "Sets the drive entity index with relative position and angle offsets.")
{
	int argVal = args.ArgC();

	if (argVal != 2 && argVal != 5 && argVal != 8)
	{
		Msg("Usage: %s <index> [position] [angles]\n", args.Arg(0));
		return;
	}
	spt_camera.driveEntInfo.entIndex = atoi(args.Arg(1));

	if (argVal >= 5)
		spt_camera.driveEntInfo.posOffsets =
		    Vector(std::stof(args.Arg(2)), std::stof(args.Arg(3)), std::stof(args.Arg(4)));
	else
		spt_camera.driveEntInfo.posOffsets = Vector(0);

	if (argVal == 8)
		spt_camera.driveEntInfo.angOffsets =
		    QAngle(std::stof(args.Arg(5)), std::stof(args.Arg(6)), std::stof(args.Arg(7)));
	else
		spt_camera.driveEntInfo.angOffsets = QAngle(0, 0, 0);
}

namespace patterns
{
	PATTERNS(ClientModeShared__OverrideView,
	         "5135",
	         "83 EC 58 E8 ?? ?? ?? ?? 85 C0 0F 84 ?? ?? ?? ?? 8B 10 56 8B 74 24 ?? 8B C8",
	         "3420",
	         "83 EC 40 E8 ?? ?? ?? ?? 85 C0 0F 84 ?? ?? ?? ?? 8B 10 56 8B 74 24 ?? 8B C8",
	         "1910503",
	         "55 8B EC 83 EC 58 E8 ?? ?? ?? ?? 85 C0 0F 84 ?? ?? ?? ?? 8B 10 56 8B 75 ?? 8B C8",
	         "7197370",
	         "55 8B EC 83 EC 58 E8 ?? ?? ?? ?? 85 C0 0F 84 ?? ?? ?? ?? 8B 10 8B C8 56");
	PATTERNS(CInputSystem__PostButtonPressedEvent,
	         "5135",
	         "8B D1 0F B6 42 ??",
	         "7197370",
	         "55 8B EC 83 EC 18 53 8B 5D ?? BA 01 00 00 00",
	         "sdk-base-2013",
	         "55 8B EC 83 EC 14 BA 01 00 00 00 53 56 8B F1 57 8B 7D ?? 8B CF 83 E1 1F 0F B6 46 ?? 69 D8 D8 05 00 00 8B C7 C1 F8 05 D3 E2 03 DE 85 54 ?? ?? 75 ??");
	PATTERNS(CInputSystem__PostButtonReleasedEvent,
	         "5135",
	         "8B 54 24 ?? 56 8B F1 0F B6 46 ??",
	         "7197370",
	         "55 8B EC 83 EC 14 BA 01 00 00 00 53 56 8B F1 57 8B 7D ?? 8B CF 83 E1 1F 0F B6 86 ?? ?? ?? ??",
	         "sdk-base-2013",
	         "55 8B EC 83 EC 14 BA 01 00 00 00 53 56 8B F1 57 8B 7D ?? 8B CF 83 E1 1F 0F B6 46 ?? 69 D8 D8 05 00 00 8B C7 C1 F8 05 D3 E2 03 DE 85 54 ?? ?? 74 ??");
	PATTERNS(
	    C_BasePlayer__ShouldDrawLocalPlayer,
	    "5135",
	    "8B 0D ?? ?? ?? ?? 8B 01 8B 50 ?? FF D2 85 C0 75 ?? E8 ?? ?? ?? ?? 84 C0 74 ?? E8 ?? ?? ?? ?? 84 C0 75 ?? 33 C0",
	    "7197370",
	    "8B 0D ?? ?? ?? ?? 85 C9 74 ?? 8B 01 8B 40 ?? FF D0 84 C0 74 ?? A1 ?? ?? ?? ?? A8 01");
	PATTERNS(
	    CInput__MouseMove,
	    "5135",
	    "83 EC 14 56 8B F1 8B 0D ?? ?? ?? ?? 8B 01 8B 40 ?? 8D 54 24 ?? 52 FF D0 8B CE E8 ?? ?? ?? ?? 8B 0D ?? ?? ?? ?? 8B 11",
	    "7197370",
	    "55 8B EC 83 EC 1C 8D 55 ?? 56 8B F1 8B 0D ?? ?? ?? ?? 52 8B 01 FF 50 ?? 8B CE E8 ?? ?? ?? ?? 8B 0D ?? ?? ?? ?? 8B 01");
	PATTERNS(C_BasePlayer__ShouldDrawThisPlayer,
	         "7197370",
	         "E8 ?? ?? ?? ?? 84 C0 75 ?? B0 01 C3 8B 0D ?? ?? ?? ?? 85 C9 74 ?? 8B 01");

} // namespace patterns

void Camera::InitHooks()
{
	HOOK_FUNCTION(client, ClientModeShared__OverrideView);
	HOOK_FUNCTION(client, CInput__MouseMove);
	HOOK_FUNCTION(client, C_BasePlayer__ShouldDrawLocalPlayer);
	HOOK_FUNCTION(inputsystem, CInputSystem__PostButtonPressedEvent);
	HOOK_FUNCTION(inputsystem, CInputSystem__PostButtonReleasedEvent);
#if defined(SSDK2013)
	// This function only exist in steampipe
	HOOK_FUNCTION(client, C_BasePlayer__ShouldDrawThisPlayer);
#endif
}

bool Camera::CanOverrideView() const
{
	return interfaces::_engine_client->IsInGame() || spt_demostuff.Demo_IsPlayingBack();
}

bool Camera::CanInput() const
{
	// Is in drive mode and spt_cam_drive
	// If playing demo automatically enables input and need right click to drive
	if (!CanOverrideView() || y_spt_cam_control.GetInt() != 1)
		return false;
	bool canInput;
	if (spt_demostuff.Demo_IsPlayingBack())
		canInput = interfaces::inputSystem->IsButtonDown(MOUSE_RIGHT);
	else
		canInput = y_spt_cam_drive.GetBool();
	return canInput && !interfaces::engine_vgui->IsGameUIVisible();
}

void Camera::RequestTimeOffsetRefresh()
{
	timeOffsetRefreshRequested = true;
}

void Camera::OverrideView(CViewSetup* viewSetup)
{
	if (_y_spt_force_fov.GetBool())
	{
		viewSetup->fov = currentCam.fov = _y_spt_force_fov.GetFloat();
	}

	playerCam.origin = viewSetup->origin;
	playerCam.angles = viewSetup->angles;
	playerCam.fov = viewSetup->fov;

	static int prevInterpType = y_spt_cam_path_interp.GetInt();
	if (prevInterpType != y_spt_cam_path_interp.GetInt())
	{
		RecomputeInterpPath();
		prevInterpType = y_spt_cam_path_interp.GetInt();
	}

	screenWidth = viewSetup->width;
	screenHeight = viewSetup->height;

	int controlType = CanOverrideView() ? y_spt_cam_control.GetInt() : 0;

	HandleDriveMode(controlType == 1);
	HandleCinematicMode(controlType == 2 && spt_demostuff.Demo_IsPlayingBack());
	HandleDriveEntityMode(controlType == 3);
	HandleTraceMode(controlType == 4);

	viewSetup->fov = currentCam.fov;
	if (controlType >= 1 && controlType <= 4)
	{
		viewSetup->origin = currentCam.origin;
		viewSetup->angles = currentCam.angles;
	}
	else
	{
		currentCam = playerCam;
	}
}

void Camera::HandleDriveMode(bool active)
{
	static bool driveActive = false;
	if (driveActive ^ active)
	{
		if (driveActive && !active)
		{
			// On drive mode end
			HandleInput(false);
		}
		else
		{
			// On drive mode start
			currentCam.angles.z = 0;
		}
	}
	HandleInput(active && CanInput());
	driveActive = active;
}

static ButtonCode_t bindToButtonCode(const char* bind)
{
	const char* key = interfaces::_engine_client->Key_LookupBinding(bind);
	if (!key)
		return BUTTON_CODE_INVALID;
	return interfaces::inputSystem->StringToButtonCode(key);
}

static const char* const binds[] =
    {"+forward", "+back", "+moveleft", "+moveright", "+moveup", "+movedown", "+duck", "+speed"};

// Sets the keyBits to state.
// Returns false if the key isn't used by cam control
bool Camera::ProcessInputKey(ButtonCode_t keyCode, bool state)
{
	for (int i = 0; i < sizeof(binds) / sizeof(char*); i++)
	{
		ButtonCode_t code = bindToButtonCode(binds[i]);
		if (code == BUTTON_CODE_INVALID)
			continue;
		if (code == keyCode)
		{
			if (state)
				keyBits |= (1 << i);
			else
				keyBits &= ~(1 << i);
			return true;
		}
	}
	return false;
}

void Camera::HandleInput(bool active)
{
	static bool inputActive = false;

	static std::chrono::time_point<std::chrono::steady_clock> lastFrame;

	if (inputActive ^ active)
	{
		if (inputActive && !active)
		{
			// On input mode end
			interfaces::vgui_input->SetCursorPos(oldCursor[0], oldCursor[1]);
		}
		else
		{
			// On input mode start
			lastFrame = std::chrono::steady_clock::now();
			interfaces::vgui_input->GetCursorPos(oldCursor[0], oldCursor[1]);

			// - current hold keys and tranfer them to keyBits
			keyBits = 0;
			for (int i = 0; i < sizeof(binds) / sizeof(char*); i++)
			{
				ButtonCode_t code = bindToButtonCode(binds[i]);
				if (code == BUTTON_CODE_INVALID)
					continue;
				if (interfaces::inputSystem->IsButtonDown(code))
				{
					char buf[32] = {0};
					snprintf(buf, sizeof(buf), "-%s %d", &binds[i][1], code);
					EngineConCmd(buf);
					keyBits |= (1 << i);
				}
			}
		}
	}

	if (active)
	{
		auto now = std::chrono::steady_clock::now();
		float realFrameTime = std::chrono::duration_cast<std::chrono::duration<float>>(now - lastFrame).count();
		lastFrame = now;

		float frameTime = realFrameTime;
		Vector movement(0.0f, 0.0f, 0.0f);

		float moveSpeed = y_spt_cam_drive_speed.GetFloat()
		                  * ((keyBits & CAM_SPEED) ? 3.0f : ((keyBits & CAM_DUCK) ? 0.5f : 1.0f));

		if (keyBits & CAM_FORWARD)
			movement.x += 1.0f;
		if (keyBits & CAM_BACK)
			movement.x -= 1.0f;
		if (keyBits & CAM_MOVELEFT)
			movement.y -= 1.0f;
		if (keyBits & CAM_MOVERIGHT)
			movement.y += 1.0f;
		if (keyBits & CAM_MOVEUP)
			movement.z += 1.0f;
		if (keyBits & CAM_MOVEDOWN)
			movement.z -= 1.0f;

		int mx, my;
		int dx = 0, dy = 0;

		if (spt_generic.IsActiveApp())
		{
			interfaces::vgui_input->GetCursorPos(mx, my);

			dx = mx - oldCursor[0];
			dy = my - oldCursor[1];

			interfaces::vgui_input->SetCursorPos(oldCursor[0], oldCursor[1]);
		}

		// Convert to pitch/yaw
		float sens = sensitivity ? sensitivity->GetFloat() : 3.0f;
		float pitch = (float)dy * 0.022f * sens;
		float yaw = -(float)dx * 0.022f * sens;

		// Apply mouse
		QAngle angles = currentCam.angles;
		angles.x += pitch;
		angles.x = clamp(angles.x, -89.0f, 89.0f);
		angles.y += yaw;
		if (angles.y > 180.0f)
			angles.y -= 360.0f;
		else if (angles.y < -180.0f)
			angles.y += 360.0f;
		currentCam.angles = angles;

		// Now apply forward, side, up

		Vector fwd, side, up;

		AngleVectors(currentCam.angles, &fwd, &side, &up);

		VectorNormalize(movement);
		movement *= moveSpeed * frameTime;

		currentCam.origin += fwd * movement.x;
		currentCam.origin += side * movement.y;
		currentCam.origin += up * movement.z;
	}
	inputActive = active;
}

void Camera::HandleDriveEntityMode(bool active)
{
	if (!active)
		return;

	IClientEntity* ent = utils::spt_clientEntList.GetEnt(driveEntInfo.entIndex);
	driveEntExists = !!(ent);
	if (!driveEntExists)
	{
		currentCam = playerCam;
		return;
	}

	const QAngle& entAngles = ent->GetAbsAngles();
	Vector forward, right, up;
	AngleVectors(entAngles, &forward, &right, &up);
	currentCam.origin = ent->GetAbsOrigin() + driveEntInfo.posOffsets.x * forward
	                    - driveEntInfo.posOffsets.y * right + driveEntInfo.posOffsets.z * up;

	matrix3x4_t rotateMatrix;
	AngleMatrix(entAngles, rotateMatrix);
	QAngle localAng = TransformAnglesToLocalSpace(entAngles, rotateMatrix);
	localAng += driveEntInfo.angOffsets;
	currentCam.angles = TransformAnglesToWorldSpace(localAng, rotateMatrix);
}

// Camera path interp code from SourceAutoRecord
// https://github.com/p2sr/SourceAutoRecord/blob/master/src/Features/Camera.cpp
static float InterpCurve(std::vector<Vector> points, float x, bool is_angles = false)
{
	enum
	{
		FIRST,
		PREV,
		NEXT,
		LAST
	};

	if (is_angles)
	{
		float oldY = 0;
		for (int i = 0; i < 4; i++)
		{
			float angDiff = points[i].y - oldY;
			angDiff += (angDiff > 180) ? -360 : (angDiff < -180) ? 360 : 0;
			points[i].y = oldY += angDiff;
			oldY = points[i].y;
		}
	}

	if (x <= points[PREV].x)
		return points[PREV].y;
	if (x >= points[NEXT].x)
		return points[NEXT].y;

	float t = (x - points[PREV].x) / (points[NEXT].x - points[PREV].x);

	switch (y_spt_cam_path_interp.GetInt())
	{
	case 1:
	{
		// Cubic spline
		float t2 = t * t, t3 = t * t * t;
		float x0 = (points[FIRST].x - points[PREV].x) / (points[NEXT].x - points[PREV].x);
		float x1 = 0, x2 = 1;
		float x3 = (points[LAST].x - points[PREV].x) / (points[NEXT].x - points[PREV].x);
		float m1 =
		    ((points[NEXT].y - points[PREV].y) / (x2 - x1) + (points[PREV].y - points[FIRST].y) / (x1 - x0))
		    / 2;
		float m2 =
		    ((points[LAST].y - points[NEXT].y) / (x3 - x2) + (points[NEXT].y - points[PREV].y) / (x2 - x1)) / 2;

		return (2 * t3 - 3 * t2 + 1) * points[PREV].y + (t3 - 2 * t2 + t) * m1
		       + (-2 * t3 + 3 * t2) * points[NEXT].y + (t3 - t2) * m2;
	}

	case 2:
	{
		// PCHIP
		float ds[4];
		float hl = 0, dl = 0;
		for (int i = 0; i < 3; i++)
		{
			float hr = points[i + 1].x - points[i].x;
			float dr = (points[i + 1].y - points[i].y) / hr;

			if (i == 0 || dl * dr < 0.0f || dl == 0.0f || dr == 0.0f)
			{
				ds[i] = 0;
			}
			else
			{
				float wl = 2 * hl + hr;
				float wr = hl + 2 * hr;
				ds[i] = (wl + wr) / (wl / dl + wr / dr);
			}

			hl = hr;
			dl = dr;
		}

		float h = points[NEXT].x - points[PREV].x;

		float t1 = (points[NEXT].x - x) / h;
		float t2 = (x - points[PREV].x) / h;

		float f1 = points[PREV].y * (3.0f * t1 * t1 - 2.0f * t1 * t1 * t1);
		float f2 = points[NEXT].y * (3.0f * t2 * t2 - 2.0f * t2 * t2 * t2);
		float f3 = ds[PREV] * h * (t1 * t1 * t1 - t1 * t1);
		float f4 = ds[NEXT] * h * (t2 * t2 * t2 - t2 * t2);

		return f1 + f2 - f3 + f4;
	}
	default:
		// Linear interp.
		return points[PREV].y + (points[NEXT].y - points[PREV].y) * t;
	}
}

std::vector<Vector> Camera::CameraInfoToPoints(float* x, CameraInfo* y, CameraInfoParameter param)
{
	std::vector<Vector> points;
	for (int i = 0; i < 4; i++)
	{
		Vector v;
		v.x = x[i];
		CameraInfo cs = y[i];
		switch (param)
		{
		case ORIGIN_X:
			v.y = cs.origin.x;
			break;
		case ORIGIN_Y:
			v.y = cs.origin.y;
			break;
		case ORIGIN_Z:
			v.y = cs.origin.z;
			break;
		case ANGLES_X:
			v.y = cs.angles.x;
			break;
		case ANGLES_Y:
			v.y = cs.angles.y;
			break;
		case ANGLES_Z:
			v.y = cs.angles.z;
			break;
		case FOV:
			v.y = cs.fov;
			break;
		}
		points.push_back(v);
	}
	return points;
}

Camera::CameraInfo Camera::InterpPath(float time)
{
	enum
	{
		FIRST,
		PREV,
		NEXT,
		LAST
	};

	// reading 4 frames closest to time
	float frameTime = time / 0.015f;
	int frames[4] = {INT_MIN, INT_MIN, INT_MAX, INT_MAX};
	for (auto const& state : keyframes)
	{
		int stateTime = state.first;
		if (frameTime - stateTime >= 0 && frameTime - stateTime < frameTime - frames[PREV])
		{
			frames[FIRST] = frames[PREV];
			frames[PREV] = stateTime;
		}
		if (stateTime - frameTime >= 0 && stateTime - frameTime < frames[NEXT] - frameTime)
		{
			frames[LAST] = frames[NEXT];
			frames[NEXT] = stateTime;
		}
		if (stateTime > frames[FIRST] && stateTime < frames[PREV])
		{
			frames[FIRST] = stateTime;
		}
		if (stateTime > frames[NEXT] && stateTime < frames[LAST])
		{
			frames[LAST] = stateTime;
		}
	}

	// x values for interpolation
	float x[4];
	for (int i = 0; i < 4; i++)
		x[i] = (float)frames[i];

	// making sure all X values are correct before reading Y values,
	// "frames" fixed for read, "x" fixed for interpolation.
	// if there's at least one cam state in the map, none of these should be MIN or MAX after this.
	if (frames[PREV] == INT_MIN)
	{
		x[PREV] = (float)frames[NEXT];
		frames[PREV] = frames[NEXT];
	}
	if (frames[NEXT] == INT_MAX)
	{
		x[NEXT] = (float)frames[PREV];
		frames[NEXT] = frames[PREV];
	}
	if (frames[FIRST] == INT_MIN)
	{
		x[FIRST] = (float)(2 * frames[PREV] - frames[NEXT]);
		frames[FIRST] = frames[PREV];
	}
	if (frames[LAST] == INT_MAX)
	{
		x[LAST] = (float)(2 * frames[NEXT] - frames[PREV]);
		frames[LAST] = frames[NEXT];
	}

	// filling Y values
	CameraInfo y[4];
	for (int i = 0; i < 4; i++)
	{
		y[i] = keyframes[frames[i]];
	}

	// interpolating each parameter
	CameraInfo interp;
	interp.origin.x = InterpCurve(CameraInfoToPoints(x, y, ORIGIN_X), frameTime);
	interp.origin.y = InterpCurve(CameraInfoToPoints(x, y, ORIGIN_Y), frameTime);
	interp.origin.z = InterpCurve(CameraInfoToPoints(x, y, ORIGIN_Z), frameTime);
	interp.angles.x = InterpCurve(CameraInfoToPoints(x, y, ANGLES_X), frameTime, true);
	interp.angles.y = InterpCurve(CameraInfoToPoints(x, y, ANGLES_Y), frameTime, true);
	interp.angles.z = InterpCurve(CameraInfoToPoints(x, y, ANGLES_Z), frameTime, true);
	interp.fov = InterpCurve(CameraInfoToPoints(x, y, FOV), frameTime);

	return interp;
}

void Camera::HandleCinematicMode(bool active)
{
	if (!active)
		return;
	if (!keyframes.size())
		return;

	if (timeOffsetRefreshRequested)
	{
		timeOffset = interfaces::engine_tool->ClientTime() - spt_demostuff.Demo_GetPlaybackTick() * 0.015f;
		timeOffsetRefreshRequested = false;
	}

	float currentTime = interfaces::engine_tool->ClientTime() - timeOffset;
	currentCam = InterpPath(currentTime);
}

void Camera::HandleTraceMode(bool active)
{
	if (!active)
		return;
#ifdef SPT_PLAYER_TRACE_ENABLED
	player_trace::GetActiveTracePos(currentCam.origin, currentCam.angles, currentCam.fov);
#endif
}

#ifdef SPT_MESH_RENDERING_ENABLED

void Camera::OnMeshRenderSignal(MeshRendererDelegate& mr)
{
	if (!y_spt_cam_path_draw.GetBool() || !spt_demostuff.Demo_IsPlayingBack())
		return;

	if (!interpPathCache.size())
		return;

	if (!interpPathMesh.Valid())
	{
		const color32 pathColor{255, 255, 255, 255};
		const color32 keyframeColor{0, 255, 0, 255};
		const float radius = 16;
		interpPathMesh = spt_meshBuilder.CreateStaticMesh(
		    [&](MeshBuilderDelegate& mb)
		    {
			    CameraInfo prev = interpPathCache[0];
			    int start = keyframes.begin()->first;
			    for (size_t i = 0; i < interpPathCache.size(); i++)
			    {
				    const CameraInfo& current = interpPathCache[i];
				    mb.AddLine(current.origin, prev.origin, pathColor);

				    if (keyframes.find(start + i) != keyframes.end())
				    {
					    float fov = clamp(current.fov, 1, 179);

					    Vector forward, right, up;
					    AngleVectors(current.angles, &forward, &right, &up);

					    Vector pos(current.origin.x, current.origin.y, current.origin.z);

					    float a = sin(fov * M_PI / 360.0) * radius;
					    float b = a;

					    float aspectRatio =
					        screenWidth ? (float)screenHeight / (float)screenWidth : 1.0;

					    b *= aspectRatio;

					    Vector vLU = pos + radius * forward - a * right + b * up;
					    Vector vRU = pos + radius * forward + a * right + b * up;
					    Vector vLD = pos + radius * forward - a * right - b * up;
					    Vector vRD = pos + radius * forward + a * right - b * up;
					    Vector vMU = vLU + (vRU - vLU) / 2;
					    Vector vMUU = vMU + 0.5 * b * up;

					    mb.AddLine(pos, vLD, keyframeColor);
					    mb.AddLine(pos, vRD, keyframeColor);
					    mb.AddLine(pos, vLU, keyframeColor);
					    mb.AddLine(pos, vRU, keyframeColor);
					    mb.AddLine(vLD, vRD, keyframeColor);
					    mb.AddLine(vRD, vRU, keyframeColor);
					    mb.AddLine(vRU, vLU, keyframeColor);
					    mb.AddLine(vLU, vLD, keyframeColor);
					    mb.AddLine(vLU, vMUU, keyframeColor);
					    mb.AddLine(vRU, vMUU, keyframeColor);
				    }
				    prev = current;
			    }
		    });
	}
	if (interfaces::debugOverlay)
	{
		for (auto& frame : keyframes)
		{
			interfaces::debugOverlay->AddTextOverlay(frame.second.origin,
			                                         NDEBUG_PERSIST_TILL_NEXT_SERVER,
			                                         "[%d]",
			                                         frame.first);
		}
	}
	mr.DrawMesh(interpPathMesh);
}

void Camera::RecomputeInterpPath()
{
	interpPathCache.clear();
	interpPathMesh.Destroy();
	if (!keyframes.size())
		return;
	int start = keyframes.begin()->first;
	int end = keyframes.rbegin()->first;
	for (int i = start; i <= end; i++)
	{
		interpPathCache.push_back(InterpPath(i * 0.015f));
	}
}

#else
void Camera::RecomputeInterpPath() {}
#endif

bool Camera::ShouldDrawPlayerModel()
{
	if (!spt_cam_draw_player.GetBool())
		return false;

	int controlType = y_spt_cam_control.GetInt();
	if (!CanOverrideView())
		return false;

	if (controlType != 1 && controlType != 2 && !(controlType == 3 && driveEntExists))
		return false;

#ifdef SPT_OVERLAY_ENABLED
	// Don't draw player model in overlay
	if (_y_spt_overlay.GetBool())
	{
		bool renderingOverlay = spt_overlay.renderingOverlay;
		if (_y_spt_overlay_swap.GetBool())
			renderingOverlay = !renderingOverlay;
		return !renderingOverlay;
	}
#endif
	return true;
}

IMPL_HOOK_THISCALL(Camera, void, ClientModeShared__OverrideView, void*, CViewSetup* viewSetup)
{
	spt_camera.ORIG_ClientModeShared__OverrideView(thisptr, viewSetup);
	spt_camera.OverrideView(viewSetup);
}

IMPL_HOOK_THISCALL(Camera, bool, C_BasePlayer__ShouldDrawLocalPlayer, void*)
{
	if (spt_camera.ShouldDrawPlayerModel())
	{
		return true;
	}
	return spt_camera.ORIG_C_BasePlayer__ShouldDrawLocalPlayer(thisptr);
}

#if defined(SSDK2013)
IMPL_HOOK_THISCALL(Camera, bool, C_BasePlayer__ShouldDrawThisPlayer, void*)
{
	// ShouldDrawLocalPlayer only decides draw view model or weapon model in steampipe
	// We need ShouldDrawThisPlayer to make player model draw
	if (spt_camera.ShouldDrawPlayerModel())
	{
		return true;
	}
	return spt_camera.ORIG_C_BasePlayer__ShouldDrawThisPlayer(thisptr);
}
#endif

IMPL_HOOK_THISCALL(Camera,
                   void,
                   CInputSystem__PostButtonPressedEvent,
                   void*,
                   InputEventType_t nType,
                   int nTick,
                   ButtonCode_t scanCode,
                   ButtonCode_t virtualCode)
{
	if (spt_camera.CanInput() && spt_camera.ProcessInputKey(scanCode, true))
		return;

	return spt_camera.ORIG_CInputSystem__PostButtonPressedEvent(thisptr, nType, nTick, scanCode, virtualCode);
}

IMPL_HOOK_THISCALL(Camera,
                   void,
                   CInputSystem__PostButtonReleasedEvent,
                   void*,
                   InputEventType_t nType,
                   int nTick,
                   ButtonCode_t scanCode,
                   ButtonCode_t virtualCode)
{
	if (spt_camera.CanInput())
		spt_camera.ProcessInputKey(scanCode, false);

	// Still call the original function so key won't get stuck.
	return spt_camera.ORIG_CInputSystem__PostButtonReleasedEvent(thisptr, nType, nTick, scanCode, virtualCode);
}

IMPL_HOOK_THISCALL(Camera, void, CInput__MouseMove, void*, void* cmd)
{
	// Block mouse inputs and stop the game from resetting cursor pos
	if (spt_camera.CanInput() || !y_spt_mousemove.GetBool())
		return;
	spt_camera.ORIG_CInput__MouseMove(thisptr, cmd);
}

bool Camera::ShouldLoadFeature()
{
	return interfaces::_engine_client != nullptr && interfaces::engine_vgui != nullptr
	       && interfaces::vgui_input != nullptr && interfaces::inputSystem != nullptr;
}

void Camera::PreHook()
{
	loadingSuccessful = ORIG_ClientModeShared__OverrideView && ORIG_CInput__MouseMove
	                    && ORIG_C_BasePlayer__ShouldDrawLocalPlayer && ORIG_CInputSystem__PostButtonPressedEvent
	                    && ORIG_CInputSystem__PostButtonReleasedEvent;
#if defined(SSDK2013)
	loadingSuccessful &= !!ORIG_C_BasePlayer__ShouldDrawThisPlayer;
#endif
}

void Camera::LoadFeature()
{
	if (ORIG_CInput__MouseMove)
	{
		InitConcommandBase(y_spt_mousemove);
	}

	if (loadingSuccessful)
	{
		InitConcommandBase(y_spt_cam_control);
		InitConcommandBase(y_spt_cam_drive);
		InitConcommandBase(y_spt_cam_drive_speed);
		InitConcommandBase(_y_spt_force_fov);
		InitConcommandBase(spt_cam_draw_player);
		InitCommand(y_spt_cam_drive_ent);
		InitCommand(y_spt_cam_setpos);
		InitCommand(y_spt_cam_setang);

		if (spt_demostuff.Demo_IsDemoPlayerAvailable() && DemoStartPlaybackSignal.Works
		    && interfaces::engine_tool)
		{
			DemoStartPlaybackSignal.Connect(this, &Camera::RequestTimeOffsetRefresh);
			InitConcommandBase(y_spt_cam_path_interp);
			InitCommand(y_spt_cam_path_setkf);
			InitCommand(y_spt_cam_path_showkfs);
			InitCommand(y_spt_cam_path_getkfs);
			InitCommand(y_spt_cam_path_rmkf);
			InitCommand(y_spt_cam_path_clear);

#ifdef SPT_MESH_RENDERING_ENABLED
			if (spt_meshRenderer.signal.Works)
			{
				spt_meshRenderer.signal.Connect(this, &Camera::OnMeshRenderSignal);
				InitConcommandBase(y_spt_cam_path_draw);
			}
#endif
		}

		sensitivity = g_pCVar->FindVar("sensitivity");
	}
}

void Camera::UnloadFeature() {}

#endif
