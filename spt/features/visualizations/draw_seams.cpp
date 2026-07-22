#include "stdafx.hpp"

#include "worldsize.h"

#include "renderer\mesh_renderer.hpp"
#include "spt\features\tracing.hpp"

#if defined(SPT_MESH_RENDERING_ENABLED) && defined(SPT_TRACE_PORTAL_ENABLED)

#include "spt\feature.hpp"
#include "spt\utils\signals.hpp"
#include "spt\features\ent_props.hpp"
#include "spt\utils\math.hpp"
#include "spt\sptlib-wrapper.hpp"
#include "spt\features\generic.hpp"
#include "spt\utils\portal_utils.hpp"
#include "spt\utils\interfaces.hpp"
#include "imgui\imgui_interface.hpp"

ConVar y_spt_draw_seams("y_spt_draw_seams", "0", FCVAR_NONE, "Draws seamshot stuff");

class DrawSeamsFeature : public FeatureWrapper<DrawSeamsFeature>
{
protected:
	void LoadFeature() override
	{
		if (spt_meshRenderer.signal.Works)
		{
			InitConcommandBase(y_spt_draw_seams);
			spt_meshRenderer.signal.Connect(this, &DrawSeamsFeature::OnMeshRenderSignal);
			SptImGuiGroup::Draw_Misc_Seams.RegisterUserCallback(
			    []() { SptImGui::CvarCheckbox(y_spt_draw_seams, "##checkbox"); });
		}
	}

	void OnMeshRenderSignal(MeshRendererDelegate& mr);
};

static DrawSeamsFeature drawSeamsFeature;

bool TestSeamshot(const Vector& cameraPos,
                  const Vector& seamPos,
                  const cplane_t& plane1,
                  const cplane_t& plane2,
                  QAngle& seamAngle)
{
	Vector diff1 = (seamPos - cameraPos);
	VectorAngles(diff1, seamAngle);
	seamAngle.x = utils::NormalizeDeg(seamAngle.x);
	seamAngle.y = utils::NormalizeDeg(seamAngle.y);

	trace_t seamTrace;
	Vector dir = seamPos - cameraPos;
	dir.NormalizeInPlace();

	Ray_t ray;
	ray.Init(cameraPos, cameraPos + dir * MAX_TRACE_LENGTH);
	interfaces::engineTraceServer->TraceRay(ray, MASK_SHOT_PORTAL, spt_tracing.GetPortalTraceFilter(), &seamTrace);

	if (seamTrace.fraction == 1.0f)
	{
		return true;
	}
	else
	{
		float dot1 = plane1.normal.Dot(seamTrace.endpos) - plane1.dist;
		float dot2 = plane2.normal.Dot(seamTrace.endpos) - plane2.dist;

		if (dot1 < 0 || dot2 < 0)
			return true;
	}

	return false;
}

bool TraceHit(const trace_t& tr, float maxDistSqr)
{
	if (tr.fraction == 1.0f)
		return false;

	float lengthSqr = (tr.endpos - tr.startpos).LengthSqr();
	return lengthSqr < maxDistSqr;
}

void FindClosestPlane(const trace_t& tr, trace_t& out, float maxDistSqr)
{
	//look for an edge point
	Vector checkDirs[4];
	const float invalidNormalLength = 1.1f;
	out.fraction = 1.0f;

	if (tr.startsolid || tr.allsolid || !tr.plane.normal.IsValid()
	    || std::abs(tr.plane.normal.LengthSqr()) > invalidNormalLength)
	{
		return;
	}

	float planeDistanceDiff = tr.plane.normal.Dot(tr.endpos) - tr.plane.dist;
	const float INVALID_PLANE_DISTANCE_DIFF = 1.0f;

	// Sometimes the trace end position isnt on the plane? Why?
	if (std::abs(planeDistanceDiff) > INVALID_PLANE_DISTANCE_DIFF)
	{
		return;
	}

	//a vector lying on a plane
	Vector upVector = Vector(0, 0, 1);

	if (tr.plane.normal.z != 0)
	{
		upVector = Vector(1, 0, 0);
		upVector -= tr.plane.normal * upVector.Dot(tr.plane.normal);
		upVector.NormalizeInPlace();

		if (!upVector.IsValid())
		{
			Warning("Invalid upvector calculated\n");
			return;
		}
	}
	checkDirs[0] = tr.plane.normal.Cross(upVector);
	//a vector crossing the previous one
	checkDirs[1] = tr.plane.normal.Cross(checkDirs[0]);

	checkDirs[0].NormalizeInPlace();
	checkDirs[1].NormalizeInPlace();

	//the rest is the inverse of other vectors to get 4 vectors in all directions
	checkDirs[2] = checkDirs[0] * -1;
	checkDirs[3] = checkDirs[1] * -1;

	for (int i = 0; i < 4; i++)
	{
		trace_t newEdgeTr;
		Ray_t ray;
		ray.Init(tr.endpos, tr.endpos + checkDirs[i] * MAX_TRACE_LENGTH);
		interfaces::engineTraceServer->TraceRay(ray,
		                                        MASK_SHOT_PORTAL,
		                                        spt_tracing.GetPortalTraceFilter(),
		                                        &newEdgeTr);

		if (TraceHit(newEdgeTr, maxDistSqr))
		{
			if (newEdgeTr.fraction < out.fraction)
			{
				out = newEdgeTr;
			}
		}
	}

	if (out.fraction < 1.0f)
	{
		// Hack the seam position to lie exactly on the intersection of the two surfaces
		out.endpos -= (out.plane.normal.Dot(out.endpos) - out.plane.dist) * out.plane.normal;
		out.endpos -= (tr.plane.normal.Dot(out.endpos) - tr.plane.dist) * tr.plane.normal;
	}
}

void DrawSeamsFeature::OnMeshRenderSignal(MeshRendererDelegate& mr)
{
	if (!y_spt_draw_seams.GetBool())
		return;
	auto player = utils::spt_serverEntList.GetPlayer();
	if (!player)
		return;

	Vector cameraPosition = utils::GetPlayerEyePosition();
	static utils::CachedField<QAngle, "CBasePlayer", "pl.v_angle", true> vangle;
	QAngle angles = *vangle.GetPtrPlayer();

	auto env = utils::GetEnvironmentPortal();
	transformThroughPortal(env, cameraPosition, angles, cameraPosition, angles);

	Vector dir;
	AngleVectors(angles, &dir);

	Ray_t ray;
	ray.Init(cameraPosition, cameraPosition + dir * MAX_TRACE_LENGTH);

	trace_t tr;
	interfaces::engineTraceServer->TraceRay(ray, MASK_SHOT_PORTAL, spt_tracing.GetPortalTraceFilter(), &tr);

	if (tr.fraction >= 1.0f)
		return;
	trace_t edgeTr;
	const float CORNER_CHECK_LENGTH_SQR = 36.0f * 36.0f;
	FindClosestPlane(tr, edgeTr, CORNER_CHECK_LENGTH_SQR);

	if (edgeTr.fraction >= 1.0f)
		return;
	const float distanceToSeam = 0.01f;
	const float smallPlaneMovement = 0.5f;
	const float bigPlaneMovement = 2.0f;

	Vector test1Start =
	    edgeTr.endpos + tr.plane.normal * smallPlaneMovement + edgeTr.plane.normal * bigPlaneMovement;
	Vector test2Start =
	    edgeTr.endpos + tr.plane.normal * bigPlaneMovement + edgeTr.plane.normal * smallPlaneMovement;

	Vector test1Target = edgeTr.endpos + tr.plane.normal * distanceToSeam;
	Vector test2Target = edgeTr.endpos + edgeTr.plane.normal * distanceToSeam;

	QAngle t1Angle, t2Angle;
	bool test1 = TestSeamshot(test1Start, test1Target, tr.plane, edgeTr.plane, t1Angle);
	bool test2 = TestSeamshot(test2Start, test2Target, tr.plane, edgeTr.plane, t2Angle);
	bool seamshot = test1 || test2;

	const int uiScale = 10;

	//calculating an edge vector for drawing
	Vector edge = edgeTr.plane.normal.Cross(tr.plane.normal);
	VectorNormalize(edge);

	mr.DrawMesh(spt_meshBuilder.CreateDynamicMesh(
	    [&](MeshBuilderDelegate& mb)
	    {
		    const color32 red{255, 0, 0, 255};
		    const color32 green{0, 255, 0, 255};

		    mb.AddLine(edgeTr.endpos - edge * uiScale,
		               edgeTr.endpos + edge * uiScale,
		               {seamshot ? green : red, false});
		    // Lines in the direction of the planes
		    mb.AddLine(edgeTr.endpos,
		               edgeTr.endpos + edgeTr.plane.normal * uiScale,
		               {test1 ? green : red, false});
		    mb.AddLine(edgeTr.endpos, edgeTr.endpos + tr.plane.normal * uiScale, {test2 ? green : red, false});
		    if (seamshot)
		    {
			    // Seamshot triangle
			    Vector midPoint = edgeTr.endpos + (edgeTr.plane.normal + tr.plane.normal) * uiScale / 2.f;
			    mb.AddLine(midPoint,
			               edgeTr.endpos + edgeTr.plane.normal * uiScale,
			               {test1 ? green : red, false});
			    mb.AddLine(midPoint,
			               edgeTr.endpos + tr.plane.normal * uiScale,
			               {test2 ? green : red, false});
		    }
	    }));
}

#endif
