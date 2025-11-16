#pragma once

#include "..\feature.hpp"
#include "..\aim\aimstuff.hpp"
#include "..\strafe\strafestuff.hpp"

typedef struct _angset_command
{
	float angle;
	bool set;
	_angset_command() : angle(0.0f), set(false) {}
} angset_command_t;

class AimFeature : public FeatureWrapper<AimFeature>
{
public:
	aim::ViewState viewState;
	void HandleAiming(float* va, bool& yawChanged, const Strafe::StrafeInput& input);
	bool DoAngleChange(float& angle, float target);
	void SetPitch(float pitch);
	void SetYaw(float yaw);
	void ResetPitchYawCommands();
	void SetJump();
	virtual bool ShouldLoadFeature() override;
	virtual void LoadFeature() override;

protected:
private:
	angset_command_t setPitch, setYaw;
};

extern AimFeature spt_aim;
