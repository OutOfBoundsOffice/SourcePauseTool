#include "stdafx.hpp"
#include "..\feature.hpp"
#include "hud.hpp"

#if defined(SPT_HUD_ENABLED) && !defined(SPT_HUD_TEXTONLY)

#include <immintrin.h>

#include "interfaces.hpp"
#include "playerio.hpp"
#include "generic.hpp"
#include "game_detection.hpp"
#include "math.hpp"
#include "signals.hpp"
#include "visualizations\imgui\imgui_interface.hpp"

#ifdef OE
#include "..\game_shared\usercmd.h"
#else
#include "usercmd.h"
#endif

#undef min
#undef max

// Draw strafe graph
class StrafeHUD : public FeatureWrapper<StrafeHUD>
{
public:
	void SetData(uintptr_t pCmd);
	void DrawHUD();

protected:
	virtual bool ShouldLoadFeature() override;

	virtual void InitHooks() override;

	virtual void LoadFeature() override;

	virtual void UnloadFeature() override;

private:
	Vector wishDir;
	std::vector<float> accels;

	struct Line
	{
		Color color;
		int start;
		int end;
	};

	struct Point
	{
		int x, y;
	};

	bool shouldUpdateLines = false;
	std::vector<Line> lines;
	std::vector<Point> points;
};

static StrafeHUD feat_strafehud;

ConVar spt_strafehud("spt_strafehud", "0", FCVAR_NONE, "Draws the strafe HUD.");
ConVar spt_strafehud_x("spt_strafehud_x", "-10", FCVAR_NONE, "The X position for the strafe HUD.");
ConVar spt_strafehud_y("spt_strafehud_y", "-10", FCVAR_NONE, "The Y position for the strafe HUD.");
ConVar spt_strafehud_size("spt_strafehud_size",
                          "256",
                          FCVAR_NONE,
                          "The width and height of the strafe HUD.",
                          true,
                          1,
                          false,
                          0);
ConVar spt_strafehud_detail_scale("spt_strafehud_detail_scale",
                                  "4",
                                  FCVAR_NONE,
                                  "The detail scale for the lines of the strafe HUD.",
                                  true,
                                  1,
                                  true,
                                  15);
ConVar spt_strafehud_lock_mode("spt_strafehud_lock_mode",
                               "1",
                               FCVAR_NONE,
                               "Lock mode used by the strafe HUD:\n"
                               "0 - view direction\n"
                               "1 - velocity direction\n"
                               "2 - absolute angles",
                               true,
                               0,
                               true,
                               2);

// Strafe stuff

static Vector GetGroundFrictionVelocity(const Strafe::PlayerData player, const Strafe::MovementVars& vars)
{
	const float friction = vars.Friction * vars.EntFriction;
	Vector vel = player.Velocity;

	const float velLen = vel.Length2D();
	if (vars.OnGround)
	{
		if (velLen >= vars.Stopspeed)
		{
			vel *= (1.0f - vars.Frametime * friction);
		}
		else if (velLen >= std::max(0.1f, vars.Frametime * vars.Stopspeed * friction))
		{
			vel -= (vel / velLen) * vars.Frametime * vars.Stopspeed * friction;
		}
		else
		{
			vel = Vector(0.0f, 0.0f, 0.0f);
		}

		if (vel.Length2D() < 1.0f)
		{
			vel = Vector(0.0f, 0.0f, 0.0f);
		}
	}

	return vel;
}

static void CreateWishDirs(const Strafe::PlayerData player,
                           const Strafe::MovementVars& vars,
                           float sinPlayerYaw,
                           float cosPlayerYaw,
                           __m128 forwardmoves,
                           __m128 sidemoves,
                           __m128& outWishDirX,
                           __m128& outWishDirY)
{
	__m128 wishDirX = sidemoves;
	__m128 wishDirY = forwardmoves;

	const __m128 lengths = _mm_sqrt_ps(_mm_add_ps(_mm_mul_ps(wishDirX, wishDirX), _mm_mul_ps(wishDirY, wishDirY)));
	const __m128 mask = _mm_cmpgt_ps(lengths, _mm_set1_ps(1.0f));
	wishDirX = _mm_blendv_ps(wishDirX, _mm_div_ps(wishDirX, lengths), mask);
	wishDirY = _mm_blendv_ps(wishDirY, _mm_div_ps(wishDirY, lengths), mask);

	const __m128 sinYaw = _mm_set1_ps(sinPlayerYaw);
	const __m128 cosYaw = _mm_set1_ps(cosPlayerYaw);

	outWishDirX = _mm_add_ps(_mm_mul_ps(sinYaw, wishDirX), _mm_mul_ps(cosYaw, wishDirY));
	outWishDirY = _mm_sub_ps(_mm_mul_ps(sinYaw, wishDirY), _mm_mul_ps(cosYaw, wishDirX));

	if (utils::DoesGameLookLikePortal())
	{
		if (!vars.OnGround && player.Velocity.Length2DSqr() > 300 * 300)
		{
			if (std::fabs(player.Velocity.x) > 150)
			{
				__m128 velX = _mm_set1_ps(player.Velocity.x);
				outWishDirX =
				    _mm_blendv_ps(outWishDirX,
				                  _mm_set1_ps(0.0f),
				                  _mm_cmplt_ps(_mm_mul_ps(velX, outWishDirX), _mm_set1_ps(0.0f)));
			}
			if (std::fabs(player.Velocity.y) > 150)
			{
				__m128 velY = _mm_set1_ps(player.Velocity.y);
				outWishDirY =
				    _mm_blendv_ps(outWishDirY,
				                  _mm_set1_ps(0.0f),
				                  _mm_cmplt_ps(_mm_mul_ps(velY, outWishDirY), _mm_set1_ps(0.0f)));
			}
		}
	}
}

static __m128 GetMaxSpeeds(const Strafe::PlayerData player,
                           const Strafe::MovementVars& vars,
                           __m128 wishDirX,
                           __m128 wishDirY,
                           bool forceOnGround)
{
	const __m128 maxSpeed = _mm_set1_ps(vars.Maxspeed);
	const __m128 wishSpeedCap = _mm_set1_ps(vars.WishspeedCap);

	const __m128 duckMultiplier = _mm_set1_ps((vars.OnGround && player.Ducking) ? 0.33333333f : 1.0f);

	wishDirX = _mm_mul_ps(wishDirX, maxSpeed);
	wishDirY = _mm_mul_ps(wishDirY, maxSpeed);

	const __m128 wishDirLen =
	    _mm_sqrt_ps(_mm_add_ps(_mm_mul_ps(wishDirX, wishDirX), _mm_mul_ps(wishDirY, wishDirY)));

	__m128 clampedMaxSpeed = _mm_min_ps(maxSpeed, wishDirLen);
	clampedMaxSpeed = _mm_mul_ps(clampedMaxSpeed, duckMultiplier);

	if (forceOnGround || vars.OnGround)
		return clampedMaxSpeed;

	return _mm_min_ps(wishSpeedCap, clampedMaxSpeed);
}

static inline __m128 GetMaxAccels(const Strafe::PlayerData player,
                                  const Strafe::MovementVars& vars,
                                  __m128 wishDirX,
                                  __m128 wishDirY)
{
	const float accel = vars.OnGround ? vars.Accelerate : vars.Airaccelerate;
	return _mm_mul_ps(_mm_set1_ps(vars.EntFriction * vars.Frametime * accel),
	                  GetMaxSpeeds(player, vars, wishDirX, wishDirY, true));
}

static void GetAccelAfterMove(const Strafe::PlayerData player,
                              const Strafe::MovementVars& vars,
                              float sinPlayerYaw,
                              float cosPlayerYaw,
                              const Vector& oldVel,
                              float oldVelLength2D,
                              const float* yaws,
                              float* outAccel)
{
	const __m128 vecYaws = _mm_loadu_ps(yaws);
	const __m128 forwardmoves = _mm_cos_ps(vecYaws);
	const __m128 sidemoves = _mm_sin_ps(vecYaws);

	__m128 wishDirX, wishDirY;
	CreateWishDirs(player, vars, sinPlayerYaw, cosPlayerYaw, forwardmoves, sidemoves, wishDirX, wishDirY);

	const __m128 wishDirLengths =
	    _mm_sqrt_ps(_mm_add_ps(_mm_mul_ps(wishDirX, wishDirX), _mm_mul_ps(wishDirY, wishDirY)));
	const __m128 zeroMask = _mm_cmpeq_ps(wishDirLengths, _mm_set1_ps(0.0f));

	const __m128 maxSpeeds = GetMaxSpeeds(player, vars, wishDirX, wishDirY, false);
	const __m128 maxAccels = GetMaxAccels(player, vars, wishDirX, wishDirY);

	const __m128 velX = _mm_set1_ps(oldVel.x);
	const __m128 velY = _mm_set1_ps(oldVel.y);

	wishDirX = _mm_div_ps(wishDirX, wishDirLengths);
	wishDirY = _mm_div_ps(wishDirY, wishDirLengths);

	const __m128 dotProducts = _mm_add_ps(_mm_mul_ps(velX, wishDirX), _mm_mul_ps(velY, wishDirY));
	const __m128 accelDiff = _mm_sub_ps(maxSpeeds, dotProducts);
	const __m128 accelMask = _mm_cmple_ps(accelDiff, _mm_set1_ps(0.0f));

	const __m128 accelForce = _mm_min_ps(maxAccels, accelDiff);

	const __m128 newVelX = _mm_add_ps(velX, _mm_mul_ps(wishDirX, accelForce));
	const __m128 newVelY = _mm_add_ps(velY, _mm_mul_ps(wishDirY, accelForce));
	const __m128 newVelLen = _mm_sqrt_ps(_mm_add_ps(_mm_mul_ps(newVelX, newVelX), _mm_mul_ps(newVelY, newVelY)));

	const __m128 accel = _mm_sub_ps(newVelLen, _mm_set1_ps(oldVelLength2D));

	const __m128 result = _mm_blendv_ps(accel, _mm_set1_ps(0.0f), _mm_or_ps(zeroMask, accelMask));

	_mm_storeu_ps(outAccel, result);
}

void StrafeHUD::SetData(uintptr_t pCmd)
{
	if (!spt_strafehud.GetBool())
		return;

	const CUserCmd* cmd = reinterpret_cast<CUserCmd*>(pCmd);
	float forwardmove = cmd->forwardmove;
	float sidemove = cmd->sidemove;
	float upmove = cmd->upmove;

	accels.clear();
	lines.clear();

	const auto player = spt_playerio.GetPlayerData();
	const auto vars = spt_playerio.GetMovementVars();
	const float playerYaw = utils::GetPlayerEyeAngles().y;

	float relAng = 0.0f;
	const int lockMode = spt_strafehud_lock_mode.GetInt();
	if (lockMode > 0)
	{
		relAng = playerYaw;
		if (lockMode == 1)
		{
			QAngle angles;
			VectorAngles(player.Velocity, Vector(0, 0, 1), angles);
			relAng -= angles.y;
		}
		relAng *= utils::M_DEG2RAD;
	}

	const float speed = forwardmove * forwardmove + sidemove * sidemove + upmove * upmove;
	if (speed > vars.Maxspeed * vars.Maxspeed)
	{
		const float ratio = vars.Maxspeed / std::sqrtf(speed);
		forwardmove *= ratio;
		sidemove *= ratio;
		upmove *= ratio;
	}

	wishDir = Vector(std::cosf(relAng) * sidemove - std::sinf(relAng) * forwardmove,
	                 std::sinf(relAng) * sidemove + std::cosf(relAng) * forwardmove,
	                 0.0f);
	const float wishDirLen = wishDir.Length2D();
	if (wishDirLen > 1.0f)
	{
		wishDir /= wishDirLen;
	}

	int detail = spt_strafehud_size.GetInt() * spt_strafehud_detail_scale.GetFloat();
	detail = ((detail + 3) / 4) * 4; // round up to multiple of 4
	accels.reserve(detail);

	const Vector oldVel = GetGroundFrictionVelocity(player, vars);

	const float sinPlayerYaw = std::sinf(playerYaw * utils::M_DEG2RAD);
	const float cosPlayerYaw = std::cosf(playerYaw * utils::M_DEG2RAD);

	for (int i = 0; i < detail; i += 4)
	{
		std::array<float, 4> ang;
		for (int j = 0; j < 4; j++)
		{
			ang[j] = ((i + j) / (float)detail) * 2.0f * M_PI + relAng;
		}

		std::array<float, 4> accel{};
		GetAccelAfterMove(player,
		                  vars,
		                  sinPlayerYaw,
		                  cosPlayerYaw,
		                  oldVel,
		                  oldVel.Length2D(),
		                  ang.data(),
		                  accel.data());
		accels.insert(accels.end(), accel.begin(), accel.end());
	}

	auto [minIt, maxIt] = std::minmax_element(accels.begin(), accels.end());
	float biggestAccel = *maxIt;
	float smallestAccel = *minIt;
	for (float& accel : accels)
	{
		if (accel > 0.0f && biggestAccel > 0.0f)
		{
			accel /= biggestAccel;
		}
		else if (accel < 0.0f && smallestAccel < 0.0f)
		{
			accel /= -smallestAccel;
		}
	}

	shouldUpdateLines = true;
}

void StrafeHUD::DrawHUD()
{
	const int pad = 5;
	int size = spt_strafehud_size.GetInt();
	int x = spt_strafehud_x.GetInt();
	int y = spt_strafehud_y.GetInt();

	if (x < 0)
		x += spt_hud_feat.screen.width - size;
	if (y < 0)
		y += spt_hud_feat.screen.height - size;

	const Color bgColor(0, 0, 0, 192);
	const Color lineColor(64, 64, 64, 255);
	const Color wishDirColor(0, 0, 255, 255);

	// Draw background
	interfaces::surface->DrawSetColor(bgColor);
	interfaces::surface->DrawFilledRect(x, y, x + size, y + size);
	x += pad;
	y += pad;
	size -= pad * 2;

	const int mid_x = x + size / 2;
	const int mid_y = y + size / 2;

	const float dx = size * 0.5f;
	const float dy = size * 0.5f;

	// Containing rect
	interfaces::surface->DrawOutlinedRect(x, y, x + size, y + size);

	// Circles
	interfaces::surface->DrawOutlinedCircle(mid_x, mid_y, size / 2, 32);
	interfaces::surface->DrawOutlinedCircle(mid_x, mid_y, size / 4, 32);

	// Half-lines and diagonals
	interfaces::surface->DrawLine(mid_x, y, mid_x, y + size);
	interfaces::surface->DrawLine(x, mid_y, x + size, mid_y);
	interfaces::surface->DrawLine(x, y, x + size, y + size);
	interfaces::surface->DrawLine(x, y + size, x + size, y);

	// Acceleration line
	if (shouldUpdateLines)
	{
		shouldUpdateLines = false;

		lines.clear();
		points.clear();

		const Color accelColor(0, 255, 0, 255);
		const Color decelColor(255, 0, 0, 255);
		const Color nocelColor(255, 255, 0, 255);

		Color prevColor(0, 0, 0);
		Line currentLine = Line{
		    .color = nocelColor,
		    .start = 0,
		    .end = 0,
		};

		const int detail = accels.size();
		for (int i = 0; i < detail; i++)
		{
			const float ang1 = (i / (float)detail) * 2.0f * M_PI;
			const int i2 = (i + 1) % detail;
			const float ang2 = (i2 / (float)detail) * 2.0f * M_PI;

			const float a1 = std::min(std::max(accels[i], -1.0f), 1.0f);
			const float a2 = std::min(std::max(accels[i2], -1.0f), 1.0f);

			const float ad1 = (a1 + 1.0f) * 0.5f;
			const float ad2 = (a2 + 1.0f) * 0.5f;

			Color currentColor = nocelColor;
			if ((ad1 != 0.0f && ad2 != 0.0f) && a1 * a2 > 0.0f)
			{
				currentColor = (a1 >= 0.0f) ? accelColor : decelColor;
			}

			if (i == 0)
			{
				// Add first point
				points.push_back(Point{mid_x + (int)(std::sinf(ang1) * dx * ad1),
				                       mid_y - (int)(std::cosf(ang1) * dy * ad1)});
			}

			if (currentColor != prevColor)
			{
				if (currentLine.start != currentLine.end)
				{
					lines.push_back(currentLine);
				}
				currentLine = Line{currentColor, (int)points.size() - 1, 0};
			}

			points.push_back(Point{
			    mid_x + (int)(std::sinf(ang2) * dx * ad2),
			    mid_y - (int)(std::cosf(ang2) * dy * ad2),
			});

			currentLine.end = (int)points.size() - 1;
			prevColor = currentColor;
		}

		points.push_back(points[0]);
		currentLine.end = (int)points.size() - 1;
		lines.push_back(currentLine);
	}

	// Draw lines
	for (const auto& line : lines)
	{
		assert(line.end > line.start);

		interfaces::surface->DrawSetColor(line.color);

		for (int i = line.start; i < line.end; i++)
		{
			const auto& point1 = points[i];
			const auto& point2 = points[i + 1];
			interfaces::surface->DrawLine(point1.x, point1.y, point2.x, point2.y);
		}
	}

	// Wish dir
	interfaces::surface->DrawSetColor(wishDirColor);
	const int x0 = mid_x;
	const int y0 = mid_y;
	const int x1 = mid_x + wishDir.x * dx;
	const int y1 = mid_y - wishDir.y * dy;

	const int halfThickness = 1;

	const float slope = (y1 - y0) / (float)(x1 - x0);
	if (std::fabs(slope) <= 1)
	{
		for (int i = -halfThickness; i <= halfThickness; i++)
		{
			interfaces::surface->DrawLine(x0, y0 + i, x1, y1 + i);
		}
	}
	else
	{
		for (int i = -halfThickness; i <= halfThickness; i++)
		{
			interfaces::surface->DrawLine(x0 + i, y0, x1 + i, y1);
		}
	}
}

bool StrafeHUD::ShouldLoadFeature()
{
	return spt_hud_feat.ShouldLoadFeature() && spt_playerio.ShouldLoadFeature();
}

void StrafeHUD::InitHooks() {}

void StrafeHUD::LoadFeature()
{
	if (!(CreateMoveSignal.Works && DecodeUserCmdFromBufferSignal.Works))
		return;
	if (!spt_playerio.PlayerIOAddressesFound())
		return;
	if (!spt_generic.ORIG_ControllerMove)
		return;

	CreateMoveSignal.Connect(this, &StrafeHUD::SetData);
	DecodeUserCmdFromBufferSignal.Connect(this, &StrafeHUD::SetData);

	bool result = spt_hud_feat.AddHudDefaultGroup(
	    HudCallback(std::bind(&StrafeHUD::DrawHUD, this), []() { return spt_strafehud.GetBool(); }, false));
	if (result)
	{
		InitConcommandBase(spt_strafehud);
		InitConcommandBase(spt_strafehud_x);
		InitConcommandBase(spt_strafehud_y);
		InitConcommandBase(spt_strafehud_size);
		InitConcommandBase(spt_strafehud_detail_scale);
		InitConcommandBase(spt_strafehud_lock_mode);
	}

	SptImGuiGroup::Hud_StrafeHud.RegisterUserCallback(
	    []()
	    {
		    ImGui::BeginDisabled(!SptImGui::CvarCheckbox(spt_strafehud, "##enabled"));

		    const char* opts[] = {"View direction", "Velocity direction", "Absolute angles"};
		    SptImGui::CvarCombo(spt_strafehud_lock_mode, "lock mode", opts, ARRAYSIZE(opts));

		    ConVar* var = &spt_strafehud_detail_scale;
		    long long varVal;
		    SptImGui::CvarsDragScalar(&var, &varVal, 1, false, "detail scale");
		    ImGui::SameLine();
		    SptImGui::CmdHelpMarkerWithName(*var);
		    var = &spt_strafehud_size;
		    SptImGui::CvarsDragScalar(&var, &varVal, 1, false, "HUD size");
		    ImGui::SameLine();
		    SptImGui::CmdHelpMarkerWithName(*var);

		    ConVar* cvars[] = {&spt_strafehud_x, &spt_strafehud_y};
		    float f[ARRAYSIZE(cvars)];
		    SptImGui::CvarsDragScalar(cvars, f, ARRAYSIZE(cvars), true, "HUD pos");
		    ImGui::SameLine();
		    SptImGui::HelpMarker("Can be set with spt_strafehud_x/y");
		    ImGui::EndDisabled();
	    });
}

void StrafeHUD::UnloadFeature() {}

#endif
