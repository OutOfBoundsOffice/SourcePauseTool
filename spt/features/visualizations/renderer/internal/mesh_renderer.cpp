/*
* This file contains the implementations for all 3 mesh renderers! Their purpose is somewhat similar to the 3
* mesh builders:
* - MeshRendererFeature (spt_meshRenderer)
* - MeshRendererDelegate
* - MeshRendererInternal (g_meshRendererInternal)
* The MeshBuilderFeature contains hooks and the mesh render signal, but is otherwise effectively stateless. At the
* start of every frame, it will signal all users and give them a stateless delegate. The user then gives this
* delegate dynamic or static meshes created with the mesh builder, and the delegate edits the state of the
* internal renderer.
* 
* At the start of a frame, we'll get a signal from spt_overlay in which we do cleanup from the previous frame,
* then we signal the users. The meshes from the users will get wrapped and queued for drawing later. We take
* advantage of two passes the game does: DrawOpaques and DrawTranslucents. Both are called for each view, and you
* may get a different view for each portal, saveglitch overlay, etc. We abstract this away from the user - when
* the user calls DrawMesh() we defer the drawing until those two passes. When rendering opaques, we can render the
* meshes in any order, but the same is not true for translucents. In that case we must figure out our own render
* order. Nothing we do will be perfect - but a good enough solution for many cases is to approximate the distance
* to each mesh from the camera as a single value and sort based on that.
* 
* This system would not exist without the absurd of times mlugg has helped me while making it; send him lots of
* love, gifts, and fruit baskets.
*/

#include "stdafx.hpp"

#include "..\mesh_renderer.hpp"
#include "mesh_renderer_internal.hpp"

#ifdef SPT_MESH_RENDERING_ENABLED

#include <algorithm>
#include <numeric>

#include "internal_defs.hpp"
#include "interfaces.hpp"
#include "signals.hpp"

#include "spt/features/visualizations/imgui/imgui_interface.hpp"

ConVar y_spt_draw_mesh_debug(
    "y_spt_draw_mesh_debug",
    "0",
    FCVAR_DONTRECORD,
    "Draws the AABB and position metric of all meshes, uses the following colors:\n"
    "   - red: static mesh\n"
    "   - blue: dynamic mesh\n"
    "   - yellow: dynamic mesh with a callback\n"
    "   - red cross: the position metric to a translucent mesh, used to determine the render order\n"
    "   - green: fused opaque dynamic meshes (only available with a cvar value of 2)\n"
    "   - light green: fused translucent dynamic meshes (only available with a cvar value of 2)");

CON_COMMAND_F(y_spt_destroy_all_static_meshes,
              "Destroy all static meshes created with the mesh builder, used for debugging",
              FCVAR_DONTRECORD)
{
	int count = spt_meshRenderer.DestroyAllStaticMeshes();
	Msg("Destroyed %d static mesh%s\n", count, count == 1 ? "" : "es");
};

#define DEBUG_COLOR_STATIC_MESH _COLOR(150, 20, 10, 255)
#define DEBUG_COLOR_DYNAMIC_MESH _COLOR(0, 0, 255, 255)
#define DEBUG_COLOR_DYNAMIC_MESH_WITH_CALLBACK _COLOR(255, 150, 50, 255)
#define DEBUG_COLOR_FUSED_DYNAMIC_MESH_OPAQUE _COLOR(0, 255, 0, 255)
#define DEBUG_COLOR_FUSED_DYNAMIC_MESH_TRANSLUCENT _COLOR(150, 255, 200, 255)
#define DEBUG_COLOR_CROSS _COLOR(255, 0, 0, 255)

/**************************************** MESH RENDERER FEATURE ****************************************/

namespace patterns
{
	PATTERNS(CRendering3dView__DrawOpaqueRenderables,
	         "5135-portal1",
	         "55 8D 6C 24 8C 81 EC 94 00 00 00",
	         "7462488-portal1",
	         "55 8B EC 81 EC 80 00 00 00 8B 15 ?? ?? ?? ??",
	         "ep3-mod",
	         "55 8B EC 83 EC 70 A1 ?? ?? ?? ?? 33 C5 89 45 ?? 53 56");
	PATTERNS(CRendering3dView__DrawTranslucentRenderables,
	         "5135-portal1",
	         "55 8B EC 83 EC 34 53 8B D9 8B 83 94 00 00 00 8B 13 56 8D B3 94 00 00 00",
	         "5135-hl2",
	         "55 8B EC 83 EC 34 83 3D ?? ?? ?? ?? 00",
	         "1910503-portal1",
	         "55 8B EC 81 EC 9C 00 00 00 53 56 8B F1 8B 86 E8 00 00 00 8B 16 57 8D BE E8 00 00 00",
	         "7462488-portal1",
	         "55 8B EC 81 EC A0 00 00 00 53 8B D9",
	         "7467727-hl2",
	         "55 8B EC 81 EC A0 00 00 00 83 3D ?? ?? ?? ?? 00",
	         "ep3-mod",
	         "55 8B EC 83 EC 3C A1 ?? ?? ?? ?? 33 C5 89 45 ?? 83 3D ?? ?? ?? ?? 00");
	PATTERNS(CSkyBoxView__DrawInternal,
	         "EZ2-1.0.0+original",
	         "55 8B EC 81 EC 28 01 00 00 A1 ?? ?? ?? ?? 33 C5 89 45 ?? 8B 45 ??",
	         "5135-portal1",
	         "83 EC 28 53 56 8B F1 8B 0D ?? ?? ?? ??",
	         "7467727-hl2",
	         "55 8B EC 83 EC 24 53 56 57 8B F9 8B 0D ?? ?? ?? ??");
} // namespace patterns

bool MeshRendererFeature::ShouldLoadFeature()
{
	if (!interfaces::materialSystem)
	{
		DevWarning("Mesh rendering not available because materialSystem was not initialized!\n");
		return false;
	}
	return true;
}

void MeshRendererFeature::InitHooks()
{
	HOOK_FUNCTION(client, CRendering3dView__DrawOpaqueRenderables);
	HOOK_FUNCTION(client, CRendering3dView__DrawTranslucentRenderables);
	HOOK_FUNCTION(client, CSkyBoxView__DrawInternal);
}

void MeshRendererFeature::PreHook()
{
	/*
	* 1) InitHooks: spt_overlay finds the render function.
	* 2) PreHook: spt_overlay may connect the RenderViewSignal. To not depend on feature load order
	*    we cannot check if the signal exists, but we can check if the RenderView function was found.
	* 3) LoadFeature: Anything that uses the mesh rendering system can check to see if the signal works.
	*/
	signal.Works = ORIG_CRendering3dView__DrawOpaqueRenderables && ORIG_CRendering3dView__DrawTranslucentRenderables
	               && spt_overlay.ORIG_CViewRender__RenderView;

	g_meshMaterialMgr.Load();
}

void MeshRendererFeature::LoadFeature()
{
	if (!signal.Works)
		return;
	RenderViewPre_Signal.Connect(&g_meshRendererInternal, &MeshRendererInternal::OnRenderViewPre_Signal);
	InitConcommandBase(y_spt_draw_mesh_debug);
	InitCommand(y_spt_destroy_all_static_meshes);
	SptImGuiGroup::Dev_Mesh.RegisterUserCallback(MeshRendererFeature::ImGuiCallback);
}

void MeshRendererFeature::UnloadFeature()
{
	signal.Clear();
	g_meshRendererInternal.FrameCleanup();
	g_meshMaterialMgr.Unload();
	StaticMesh::DestroyAll();
}

void MeshRendererFeature::ImGuiCallback()
{
	extern ConVar y_spt_draw_mesh_examples;
	SptImGui::CvarCheckbox(y_spt_draw_mesh_examples, "Draw mesh examples");

	static int lastDestroyedCount = 0;
	static double lastDestroyedTime = -666.f;
	if (SptImGui::CmdButton("Destroy all static meshes", y_spt_destroy_all_static_meshes_command))
	{
		lastDestroyedCount = StaticMesh::DestroyAll();
		lastDestroyedTime = ImGui::GetTime();
	}
	bool emph = ImGui::GetTime() - lastDestroyedTime <= .5;
	ImGui::TextColored(emph ? ImVec4{.8f, .8f, .3f, 1.f} : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
	                   "(%d mesh%s destroyed last time)",
	                   lastDestroyedCount,
	                   lastDestroyedCount == 1 ? "" : "es");

	const char* const opts[] = {
	    "Disabled",
	    "Draw AABB for meshes",
	    "Draw AABB for meshes & fused meshes",
	};
	SptImGui::CvarCombo(y_spt_draw_mesh_debug, "##draw_mesh_debug", opts, ARRAYSIZE(opts));

	const auto& allVerts = g_meshBuilderInternal.sharedLists.simple.verts;
	const auto& allIndices = g_meshBuilderInternal.sharedLists.simple.indices;

	size_t totalVerts = std::accumulate(allVerts.cbegin(),
	                                    allVerts.cend(),
	                                    0,
	                                    [](auto a, auto& vData) { return a + vData.size(); });

	size_t totalIndices = std::accumulate(allIndices.cbegin(),
	                                      allIndices.cend(),
	                                      0,
	                                      [](auto a, auto& vData) { return a + vData.size(); });

	ImGui::Text("%3u dynamic wrappers this frame: %6u verts %6u indices",
	            g_meshRendererInternal.nDynamicsThisFrame,
	            totalVerts,
	            totalIndices);
	ImGui::Text("%3u static mesh wrappers this frame", g_meshRendererInternal.nStaticsThisFrame);
}

int MeshRendererFeature::CurrentPortalRenderDepth() const
{
	return MAX(0, (int)g_meshRendererInternal.debugMeshInfo.descriptionSlices.size() - 1);
}

int MeshRendererFeature::DestroyAllStaticMeshes() const
{
	return StaticMesh::DestroyAll();
}

IMPL_HOOK_THISCALL(MeshRendererFeature, void, CRendering3dView__DrawOpaqueRenderables, CRendering3dView*, int param)
{
	// HACK - param is a bool in SSDK2007 and enum in SSDK2013
	// render order shouldn't matter here
	spt_meshRenderer.ORIG_CRendering3dView__DrawOpaqueRenderables(thisptr, param);
	if (spt_meshRenderer.signal.Works)
		g_meshRendererInternal.OnDrawOpaques(thisptr);
}

IMPL_HOOK_THISCALL(MeshRendererFeature,
                   void,
                   CRendering3dView__DrawTranslucentRenderables,
                   CRendering3dView*,
                   bool inSkybox,
                   bool shadowDepth)
{
	// render order matters here, render our stuff on top of other translucents
	spt_meshRenderer.ORIG_CRendering3dView__DrawTranslucentRenderables(thisptr, inSkybox, shadowDepth);
	if (spt_meshRenderer.signal.Works)
		g_meshRendererInternal.OnDrawTranslucents(thisptr);
}

#ifdef SSDK2007
IMPL_HOOK_THISCALL(MeshRendererFeature,
                   void,
                   CSkyBoxView__DrawInternal,
                   void*,
                   view_id_t id,
                   bool bInvoke,
                   ITexture* pRenderTarget)
{
	g_meshRendererInternal.renderingSkyBox = true;
	spt_meshRenderer.ORIG_CSkyBoxView__DrawInternal(thisptr, id, bInvoke, pRenderTarget);
	g_meshRendererInternal.renderingSkyBox = false;
}
#else
IMPL_HOOK_THISCALL(MeshRendererFeature,
                   void,
                   CSkyBoxView__DrawInternal,
                   void*,
                   view_id_t id,
                   bool bInvoke,
                   ITexture* pRenderTarget,
                   ITexture* pDepthTarget)
{
	g_meshRendererInternal.renderingSkyBox = true;
	spt_meshRenderer.ORIG_CSkyBoxView__DrawInternal(thisptr, id, bInvoke, pRenderTarget, pDepthTarget);
	g_meshRendererInternal.renderingSkyBox = false;
}
#endif

/**************************************** MESH UNIT WRAPPER ****************************************/

MeshUnitWrapper::MeshUnitWrapper(DynamicMeshToken dynamicToken, const RenderCallback& callback)
    : _staticMeshPtr(nullptr)
    , _dynamicToken(dynamicToken)
    , callback(callback)
    , posInfo(g_meshBuilderInternal.GetDynamicMeshFromToken(dynamicToken).posInfo)
{
}

MeshUnitWrapper::MeshUnitWrapper(const std::shared_ptr<StaticMeshUnit>& staticMeshPtr, const RenderCallback& callback)
    : _staticMeshPtr(staticMeshPtr), _dynamicToken{}, callback(callback), posInfo(staticMeshPtr->posInfo)
{
}

// returns true if this unit should be rendered
bool MeshUnitWrapper::ApplyCallbackAndCalcCamDist()
{
	auto& viewInfo = g_meshRendererInternal.viewInfo;

	if (callback)
	{
		// apply callback

		const MeshPositionInfo& unitPosInfo =
		    _staticMeshPtr ? _staticMeshPtr->posInfo
		                   : g_meshBuilderInternal.GetDynamicMeshFromToken(_dynamicToken).posInfo;

		CallbackInfoIn infoIn = {
		    *viewInfo.viewSetup,
		    unitPosInfo,
		    spt_meshRenderer.CurrentPortalRenderDepth(),
		    spt_overlay.renderingOverlay,
		};

		callback(infoIn, cbInfoOut = CallbackInfoOut{});

		if (cbInfoOut.skipRender || cbInfoOut.colorModulate.a == 0)
			return false;
		TransformAABB(cbInfoOut.mat, unitPosInfo.mins, unitPosInfo.maxs, posInfo.mins, posInfo.maxs);
	}

	// do frustum check

	auto& frustum = g_meshRendererInternal.viewInfo.frustum;
	for (int i = 0; i < 6; i++)
		if (BoxOnPlaneSide((float*)&posInfo.mins, (float*)&posInfo.maxs, &frustum[i]) == 2)
			return false;

	// calc camera to mesh "distance"

	CalcClosestPointOnAABB(posInfo.mins, posInfo.maxs, viewInfo.viewSetup->origin, camDistSqrTo);
	if (viewInfo.viewSetup->origin == camDistSqrTo)
		camDistSqrTo = (posInfo.mins + posInfo.maxs) / 2.f; // if inside cube, use center idfk
	camDistSqr = viewInfo.viewSetup->origin.DistToSqr(camDistSqrTo);

	return true;
}

// We pretend this mesh wrapper contains a mesh from this unit, but it could be a fused mesh.
// All that matters is that we use its material and our callback.
void MeshUnitWrapper::Render(const IMeshWrapper mw)
{
	if (!mw.iMesh)
		return;

	CMatRenderContextPtr context{interfaces::materialSystem};
	if (callback)
	{
		color32 cMod = cbInfoOut.colorModulate;
		mw.material->ColorModulate(cMod.r / 255.f, cMod.g / 255.f, cMod.b / 255.f);
		mw.material->AlphaModulate(cMod.a / 255.f);
		context->MatrixMode(MATERIAL_MODEL);
		context->PushMatrix();
		context->LoadMatrix(cbInfoOut.mat);
	}
	else
	{
		mw.material->ColorModulate(1, 1, 1);
		mw.material->AlphaModulate(1);
	}
	context->Bind(mw.material);
	mw.iMesh->Draw();
	if (!_staticMeshPtr)
		context->Flush();
	if (callback)
		context->PopMatrix();
}

/**************************************** MESH RENDER FEATURE ****************************************/

void MeshRendererInternal::FrameCleanup()
{
	queuedUnitWrappers.clear();
	g_meshBuilderInternal.FrameCleanup();
	Assert(debugMeshInfo.descriptionSlices.empty());
	nDynamicsThisFrame = 0;
	nStaticsThisFrame = 0;
}

void MeshRendererInternal::OnRenderViewPre_Signal(void* thisptr, CViewSetup* cameraView)
{
	// ensure we only run once per frame
	if (spt_overlay.renderingOverlay || !spt_meshRenderer.signal.Works)
		return;
	SPT_VPROF_BUDGET(__FUNCTION__, VPROF_BUDGETGROUP_MESH_RENDERER);
	FrameCleanup();
	frameNum++;
	inSignal = true;
	MeshRendererDelegate renderDelgate{};
	spt_meshRenderer.signal(renderDelgate);
	inSignal = false;
}

void MeshRendererInternal::SetupViewInfo(CRendering3dView* rendering3dView)
{
	viewInfo.rendering3dView = rendering3dView;

	// if only more stuff was public ://

	viewInfo.viewSetup = (CViewSetup*)((uintptr_t)rendering3dView + 8);

	// these are inward facing planes, convert from VPlane to cplane_t for fast frustum test
	for (int i = 0; i < FRUSTUM_NUMPLANES; i++)
	{
		VPlane* vp = *(VPlane**)(viewInfo.viewSetup + 1) + i;
		viewInfo.frustum[i] = {.normal = vp->m_Normal, .dist = vp->m_Dist, .type = 255};
		viewInfo.frustum[i].signbits = SignbitsForPlane(&viewInfo.frustum[i]);
	}
}

void MeshRendererInternal::OnDrawOpaques(CRendering3dView* renderingView)
{
	// if there's any use for rendering stuff in sky boxes in the future we can add this to the callback info
	if (renderingSkyBox)
		return;

	SPT_VPROF_BUDGET(__FUNCTION__, VPROF_BUDGETGROUP_MESH_RENDERER);
	SetupViewInfo(renderingView);

	// push a new debug slice, the corresponding pop is at the end of DrawTranslucents
	debugMeshInfo.descriptionSlices.emplace(debugMeshInfo.sharedDescriptionList);

	static std::vector<MeshComponent> components;
	CollectRenderableComponents(components, true);
	std::ranges::stable_sort(components, std::less{});
	DrawAll(components, y_spt_draw_mesh_debug.GetBool(), true);
	components.clear();
}

void MeshRendererInternal::OnDrawTranslucents(CRendering3dView* renderingView)
{
	if (renderingSkyBox)
		return;

	SPT_VPROF_BUDGET(__FUNCTION__, VPROF_BUDGETGROUP_MESH_RENDERER);
	SetupViewInfo(renderingView);

	static std::vector<MeshComponent> components;
	CollectRenderableComponents(components, false);

	/*
	* Translucent meshes must be sorted by distance first which makes them not as good for fusing. In theory
	* there should be a way to ignore the position metric if the mesh units don't overlap in screen space, but
	* I couldn't get that to work (maybe because such a comparison would not be transitive?). I think this sort
	* should be stable because I want components from the same unit to remain the same order relative to each
	* other in case <=> returns equivalent. I think this could be useful if I decide to spill components in case
	* of overflow.
	*/
	std::ranges::stable_sort(components,
	                         [](const MeshComponent& a, const MeshComponent& b)
	                         {
		                         IMaterial* matA = a.vertData ? a.vertData->material : a.iMeshWrapper.material;
		                         IMaterial* matB = b.vertData ? b.vertData->material : b.iMeshWrapper.material;
		                         if (matA != matB)
		                         {
			                         bool ignoreZA = matA->GetMaterialVarFlag(MATERIAL_VAR_IGNOREZ);
			                         bool ignoreZB = matB->GetMaterialVarFlag(MATERIAL_VAR_IGNOREZ);
			                         if (ignoreZA != ignoreZB)
				                         return ignoreZA < ignoreZB;
		                         }
		                         float distA = a.unitWrapper->camDistSqr;
		                         float distB = b.unitWrapper->camDistSqr;
		                         if (distA
		                             != distB) // same distance likely means that these are from the same unit
			                         return distA > distB;
		                         return a < b;
	                         });

	DrawAll(components, y_spt_draw_mesh_debug.GetBool(), false);

	if (y_spt_draw_mesh_debug.GetBool())
	{
		AddDebugCrosses(debugMeshInfo.descriptionSlices.top(), components);
		DrawDebugMeshes(debugMeshInfo.descriptionSlices.top()); // draw all translucent!!! debug meshes
	}
	debugMeshInfo.descriptionSlices.pop();
	components.clear();
}

void MeshRendererInternal::CollectRenderableComponents(std::vector<MeshComponent>& components, bool opaques)
{
	SPT_VPROF_BUDGET(__FUNCTION__, VPROF_BUDGETGROUP_MESH_RENDERER);
	// go through all components of all queued meshes and return those that are eligable for rendering right now
	for (MeshUnitWrapper& unitWrapper : queuedUnitWrappers)
	{
		if (!unitWrapper.ApplyCallbackAndCalcCamDist())
			continue; // the mesh is outside our frustum or the user wants to skip rendering

		if (unitWrapper.callback && opaques && unitWrapper.cbInfoOut.colorModulate.a < 1)
			continue; // color modulation forces all meshes in this unit to be translucent

		auto shouldRender = [&unitWrapper, opaques](IMaterial* material)
		{
			if (!material)
				return false;

			if (unitWrapper.callback)
				if (unitWrapper.cbInfoOut.colorModulate.a < 255)
					return !opaques; // callback changed alpha component, make translucent if < 1 otherwise opaque

			bool opaqueMaterial = !material->GetMaterialVarFlag(MATERIAL_VAR_IGNOREZ)
			                      && !material->GetMaterialVarFlag(MATERIAL_VAR_VERTEXALPHA);

			return opaques == opaqueMaterial;
		};

		if (unitWrapper._staticMeshPtr)
		{
			auto& unit = *unitWrapper._staticMeshPtr;
			for (size_t i = 0; i < unit.nMeshes; i++)
				if (shouldRender(unit.meshesArr[i].material))
					components.emplace_back(&unitWrapper, (MeshVertData*)0, unit.meshesArr[i]);
		}
		else
		{
			auto& unit = g_meshBuilderInternal.GetDynamicMeshFromToken(unitWrapper._dynamicToken);
			for (MeshVertData& vData : unit.vDataSlice)
				if (shouldRender(vData.material))
					components.emplace_back(&unitWrapper, &vData, IMeshWrapper{});
		}
	}
}

void MeshRendererInternal::AddDebugCrosses(DebugDescList& debugList, std::span<const MeshComponent> span)
{
	for (auto& mc : span)
	{
		const MeshPositionInfo& posInfo = mc.unitWrapper->posInfo;
		float maxDiameter = VectorMaximum(posInfo.maxs - posInfo.mins);
		const float smallest = 1, biggest = 15, falloff = 100;
		// scale cross by the AABB size, plot this bad boy in desmos as a function of maxBoxDim
		float size = -falloff * (biggest - smallest) / (maxDiameter + falloff) + biggest;

		debugList.emplace_back(mc.unitWrapper->camDistSqrTo, size, DEBUG_COLOR_CROSS);
	}
}

void MeshRendererInternal::DrawDebugMeshes(DebugDescList& debugList)
{
	/*
	* This is like a miniature version of what the whole renderer does, but we don't have to worry about drawing
	* the meshes in DrawOpaques() and DrawTranslucents(). We create the dynamic debug meshes for this view, wrap
	* them up in unit wrappers, sort them (which should do nothing since they all use the same materials), and
	* batch them together.
	*/

	static std::vector<MeshUnitWrapper> debugUnitWrappers;

	for (auto& debugDesc : debugList)
	{
		debugUnitWrappers.emplace_back(spt_meshBuilder.CreateDynamicMesh(
		    [&](MeshBuilderDelegate& mb)
		    {
			    if (debugDesc.isBox)
			    {
				    mb.AddBox(vec3_origin,
				              debugDesc.box.mins,
				              debugDesc.box.maxs,
				              vec3_angle,
				              {C_WIRE(debugDesc.color), false, false});
			    }
			    else
			    {
				    mb.AddCross(debugDesc.cross.crossPos,
				                debugDesc.cross.size,
				                {debugDesc.color, false});
			    }
		    }));
	}

	static std::vector<MeshComponent> debugComponents;

	for (MeshUnitWrapper& debugMesh : debugUnitWrappers)
	{
		Assert(!debugMesh._staticMeshPtr);

		auto& debugUnit = g_meshBuilderInternal.GetDynamicMeshFromToken(debugMesh._dynamicToken);

		for (auto& component : debugUnit.vDataSlice)
			if (component.indices.size() > 0)
				debugComponents.emplace_back(&debugMesh, &component, IMeshWrapper{});
	}
	std::ranges::stable_sort(debugComponents, std::less{});
	DrawAll(debugComponents, false, true);

	debugUnitWrappers.clear();
	debugComponents.clear();
}

void MeshRendererInternal::DrawAll(std::span<const MeshComponent> span, bool addDebugMeshes, bool opaques)
{
	if (span.empty())
		return;

	SPT_VPROF_BUDGET(__FUNCTION__, VPROF_BUDGETGROUP_MESH_RENDERER);
	/*
	* We create a subspan of fullSpan: compatSpan. Our goal is to give spans to the builder that can be
	* fused together. Static meshes can't be fused (they're already IMesh* objects), so we render those
	* one at a time. For dynamic meshes, we find the first element such that (first <=> elem) != 0. We
	* rely on operator<=> to tell us if two components are eligable for fusing.
	*/

	while (!span.empty())
	{
		if (span.front().vertData)
		{
			auto uncompatIt = span.begin() + 1;
			while (uncompatIt != span.end() && (span.front() <=> *uncompatIt) == 0)
				++uncompatIt;

			std::span<const MeshComponent> compatSpan{span.begin(), uncompatIt};
			g_meshBuilderInternal.fuser.BeginIMeshCreation(compatSpan, true);
			IMeshWrapper mw;
			while (mw = g_meshBuilderInternal.fuser.GetNextIMeshWrapper(), mw.iMesh)
			{
				span.begin()->unitWrapper->Render(mw);
				if (addDebugMeshes)
				{
					AddDebugBox(debugMeshInfo.descriptionSlices.top(),
					            g_meshBuilderInternal.fuser.lastFusedSpan,
					            opaques);
				}
			}
			span = span.subspan(compatSpan.size());
		}
		else
		{
			// a single static mesh
			span.front().unitWrapper->Render(span.front().iMeshWrapper);
			if (addDebugMeshes)
				AddDebugBox(debugMeshInfo.descriptionSlices.top(), span.first(1), opaques);
			span = span.subspan(1);
		}
	}
}

void MeshRendererInternal::AddDebugBox(DebugDescList& debugList, std::span<const MeshComponent> span, bool opaques)
{
	if (span.begin()->vertData)
	{
		Vector batchedMins{INFINITY};
		Vector batchedMaxs{-INFINITY};
		for (auto& mc : span)
		{
			VectorMin(mc.unitWrapper->posInfo.mins, batchedMins, batchedMins);
			VectorMax(mc.unitWrapper->posInfo.maxs, batchedMaxs, batchedMaxs);

			debugList.emplace_back(mc.unitWrapper->posInfo.mins - Vector{1.f},
			                       mc.unitWrapper->posInfo.maxs + Vector{1.f},
			                       mc.unitWrapper->callback ? DEBUG_COLOR_DYNAMIC_MESH_WITH_CALLBACK
			                                                : DEBUG_COLOR_DYNAMIC_MESH);
		}
		if (span.size() > 1 && y_spt_draw_mesh_debug.GetInt() >= 2)
		{
			debugList.emplace_back(batchedMins - Vector{opaques ? 2.5f : 2.f},
			                       batchedMaxs + Vector{opaques ? 2.5f : 2.f},
			                       opaques ? DEBUG_COLOR_FUSED_DYNAMIC_MESH_OPAQUE
			                               : DEBUG_COLOR_FUSED_DYNAMIC_MESH_TRANSLUCENT);
		}
	}
	else
	{
		Assert(span.size() == 1);
		debugList.emplace_back(span.front().unitWrapper->posInfo.mins - Vector{1.f},
		                       span.front().unitWrapper->posInfo.maxs + Vector{1.f},
		                       DEBUG_COLOR_STATIC_MESH);
	}
}

/**************************************** SPACESHIP ****************************************/

/*
* This operator has three purposes:
* 1) Determine if two components may be fused (must evaluate to std::weak_ordering::equivalent)
* 2) Sort lists of components so that consecutive elements are eligable for fusion
* 3) Order components in a way to reduce overhead (e.g. grouping the same color modulation together)
* 
* For example, using this operator to sort a list of components will order them sort of like this:
* 
*                              V 0-3 can be fused V     V 4-8 can be fused V    static V      static V
* std::vector<MeshComponent>: [0]---[1]---[2]----[3]---[4]---[5]---[7]----[8]---[9]---[10]---[11]---[12]
*                                                            static (can't fuse) ^     static ^
* 
* After sorting, the renderer will iterate over the spans [0,4), [4,9), [9,10), [10,11), [11,12), [12,13) and
* render them. The first two spans have dynamic meshes, so they would be given to the builder to attempt fusion.
* The last four are statics; they would be ordererd in a way to minimize context switching e.g. [9], [10], [11]
* would have the same material & color modulation.
*/
std::weak_ordering MeshComponent::operator<=>(const MeshComponent& rhs) const
{
	using W = std::weak_ordering;
	// group dynamics together
	if (!vertData != !rhs.vertData)
		return !vertData <=> !rhs.vertData;
	// group the same primitive type together
	// between dynamics
	if (vertData && rhs.vertData)
	{
		if (vertData->type != rhs.vertData->type)
			return vertData->type <=> rhs.vertData->type;
		// group those without a callback together
		if (!unitWrapper->callback != !rhs.unitWrapper->callback)
			return !unitWrapper->callback <=> !rhs.unitWrapper->callback;
		// if both have a callback, only group together if it's the same unit (the callback will be the same)
		if (unitWrapper->callback && rhs.unitWrapper->callback)
			return unitWrapper <=> rhs.unitWrapper;
	}
	else
	{
		// between statics, if both are in the same unit they'll have the same material, callback, colormod, etc.
		if (&unitWrapper == &rhs.unitWrapper)
			return W::equivalent;
	}
	// group the same materials together
	IMaterial* matA = vertData ? vertData->material : iMeshWrapper.material;
	IMaterial* matB = rhs.vertData ? rhs.vertData->material : rhs.iMeshWrapper.material;
	if (matA != matB)
		return matA <=> matB;
	// group the same color mod for a material together, switching color mod seems to be very slow
	if (unitWrapper->callback && rhs.unitWrapper->callback)
	{
		return *reinterpret_cast<int*>(&unitWrapper->cbInfoOut.colorModulate)
		       <=> *reinterpret_cast<int*>(&rhs.unitWrapper->cbInfoOut.colorModulate);
	}
	return W::equivalent;
}

/**************************************** MESH RENDERER DELEGATE ****************************************/

void MeshRendererDelegate::DrawMesh(const DynamicMesh& dynamicMesh, const RenderCallback& callback)
{
	if (!g_meshRendererInternal.inSignal)
	{
		AssertMsg(0, "spt: Meshes can only be drawn in MeshRenderSignal!");
		return;
	}
	if (dynamicMesh.createdFrame != g_meshRendererInternal.frameNum)
	{
		AssertMsg(0, "spt: Attempted to reuse a dynamic mesh between frames");
		Warning("spt: Can only draw dynamic meshes on the frame they were created!\n");
		return;
	}
	const DynamicMeshUnit& meshUnit = g_meshBuilderInternal.GetDynamicMeshFromToken(dynamicMesh);
	if (!meshUnit.vDataSlice.empty())
	{
		g_meshRendererInternal.queuedUnitWrappers.emplace_back(dynamicMesh, callback);
		g_meshRendererInternal.nDynamicsThisFrame++;
	}
}

void MeshRendererDelegate::DrawMesh(const StaticMesh& staticMesh, const RenderCallback& callback)
{
	if (!g_meshRendererInternal.inSignal)
	{
		AssertMsg(0, "spt: Meshes can only be drawn in MeshRenderSignal!");
		return;
	}
	if (!staticMesh.Valid())
	{
		AssertMsg(0, "spt: This static mesh has been destroyed!");
		Warning("spt: Attempting to draw an invalid static mesh!\n");
		return;
	}
	if (staticMesh.meshPtr->nMeshes > 0)
	{
		g_meshRendererInternal.queuedUnitWrappers.emplace_back(staticMesh.meshPtr, callback);
		g_meshRendererInternal.nStaticsThisFrame++;
	}
}

#endif
