#include "stdafx.hpp"

#include <inttypes.h>
#include <bitset>

#include "tr_imgui.hpp"
#include "tr_entity_cache.hpp"

#ifdef SPT_PLAYER_TRACE_ENABLED

#include "spt/utils/game_detection.hpp"
#include "spt/features/visualizations/imgui/imgui_interface.hpp"

#define _VEC_FMT "<%f, %f, %f>"
#define _VEC_UNP(v) (v).x, (v).y, (v).z

using namespace player_trace;

static void TrShowActiveTick(tr_tick activeTick)
{
	auto& tr = TrReadContextScope::Current();
	if (tr.hasStartRecordingBeenCalled)
	{
		ImGui::Text("Active tick: %" PRIu32 "/%" PRIu32,
		            activeTick,
		            tr.numRecordedTicks == 0 ? 0 : tr.numRecordedTicks - 1);
	}
	else
	{
		ImGui::TextColored(SPT_IMGUI_WARN_COLOR_YELLOW,
		                   "%s",
		                   ICON_CI_WARNING " No active trace " ICON_CI_WARNING);
	}
}

void tr_imgui::PlayerTabCallback(tr_tick activeTick)
{
	TrShowActiveTick(activeTick);
	auto& tr = TrReadContextScope::Current();

	tr_struct_version playerExportVersion = tr.GetFirstExportVersion<TrPlayerData>();
	auto plIdx = tr.GetAtTick<TrPlayerData>(activeTick);
	TrPlayerData defaultPl{};
	auto& pl = plIdx.IsValid() ? **plIdx : defaultPl;
	Vector vecInvalid{NAN, NAN, NAN};
	QAngle angInvalid{NAN, NAN, NAN};
	TrTransform transInvalid{};

	Vector eyePos, sgEyePos, vPhysPos;
	QAngle eyeAng, sgEyeAng, vPhysAng;
	pl.transVPhysIdx.GetOrDefault(transInvalid).GetPosAng(vPhysPos, vPhysAng);
	pl.transEyesIdx.GetOrDefault(transInvalid).GetPosAng(eyePos, eyeAng);
	pl.transSgEyesIdx.GetOrDefault(transInvalid).GetPosAng(sgEyePos, sgEyeAng);

	ImGui::Text("QPhys pos: " _VEC_FMT, _VEC_UNP(pl.qPosIdx.GetOrDefault(vecInvalid)));
	ImGui::Text("VPhys pos: " _VEC_FMT, _VEC_UNP(vPhysPos));
	ImGui::Text("VPhys ang: " _VEC_FMT, _VEC_UNP(vPhysAng));
	ImGui::Text("QPhys vel: " _VEC_FMT, _VEC_UNP(pl.qVelIdx.GetOrDefault(vecInvalid)));
	if (playerExportVersion >= 2)
		ImGui::Text("VPhys vel: " _VEC_FMT, _VEC_UNP(pl.vVelIdx.GetOrDefault(vecInvalid)));
	else
		ImGui::TextDisabled("VPhys vel: data was not recorded on this trace version");
	ImGui::Text("Eye pos: " _VEC_FMT, _VEC_UNP(eyePos));
	ImGui::Text("Eye ang: " _VEC_FMT, _VEC_UNP(eyeAng));
	if (utils::DoesGameLookLikePortal())
	{
		if (playerExportVersion >= 2)
		{
			if (pl.envPortalHandle.IsValid())
			{
				ImGui::Text("Portal environment: %d, (serial: %d)",
				            pl.envPortalHandle.GetEntryIndex(),
				            pl.envPortalHandle.GetSerialNumber());
			}
			else
			{
				ImGui::TextDisabled("Portal environment: NULL");
			}
		}
		else
		{
			ImGui::TextDisabled("Portal environment: data was not recorded on this trace version");
		}
		if (eyePos == sgEyePos)
		{
			ImGui::TextDisabled("SG eye pos: same as eye pos");
			ImGui::TextDisabled("SG eye ang: same as eye ang");
		}
		else
		{
			ImGui::Text("SG eye pos: " _VEC_FMT, _VEC_UNP(sgEyePos));
			ImGui::Text("SG eye ang: " _VEC_FMT, _VEC_UNP(sgEyeAng));
		}
	}
	ImGui::Text("m_fFlags: %d", pl.m_fFlags);
	ImGui::Text("fov: %" PRIu32, pl.fov);
	ImGui::Text("health: %" PRIu32, pl.m_iHealth);
	ImGui::Text("life state: %" PRIu32, pl.m_lifeState);
	ImGui::Text("collision group: %" PRIu32, pl.m_CollisionGroup);
	ImGui::Text("move type: %" PRIu32, pl.m_MoveType);

	auto contactSp = *pl.contactPtsSp;

	ImGui::Text("%" PRIu32 " contact point%s%s",
	            contactSp.size(),
	            contactSp.size() == 1 ? "" : "s",
	            contactSp.empty() ? "" : ":");

	ImGui::Indent();

	for (auto contactIdx : contactSp)
	{
		const TrPlayerContactPoint& pcp = contactIdx.GetOrDefault();
		if (SptImGui::BeginBordered())
		{
			ImGui::PushID((int)contactIdx);
			ImGui::Text("object: '%s' (player is object %d)", *pcp.objNameIdx, !pcp.playerIsObj0);
			ImGui::Text("pos: " _VEC_FMT, _VEC_UNP(pcp.posIdx.GetOrDefault(vecInvalid)));
			ImGui::Text("normal: " _VEC_FMT, _VEC_UNP(pcp.normIdx.GetOrDefault(vecInvalid)));
			ImGui::Text("force: %f", pcp.force);
			ImGui::PopID();
			SptImGui::EndBordered();
		}
	}

	ImGui::Unindent();
}

void tr_imgui::EntityTabCallback(tr_tick activeTick)
{
	TrShowActiveTick(activeTick);
	auto& tr = TrReadContextScope::Current();

	auto& entCache = tr.GetEntityCache();
	auto& entMap = entCache.GetEnts(activeTick);

	static std::vector<std::pair<TrIdx<TrEnt>, TrIdx<TrEntTransform>>> sortedEnts;
	sortedEnts.resize(entMap.size());
	std::ranges::transform(entMap, sortedEnts.begin(), std::identity{});
	std::ranges::sort(sortedEnts, std::less{}, [](auto& p) { return p.first->handle.GetEntryIndex(); });

	ImGui::TextUnformatted(ICON_CI_FILTER " Filter:");
	ImGui::SameLine();

	static ImGuiTextFilter filter;
	filter.Draw("##filter");
	ImGui::SetItemTooltip(
	    "Filter usage:\n"
	    "  \"\"         display all lines\n"
	    "  \"xxx\"      display lines containing \"xxx\"\n"
	    "  \"xxx,yyy\"  display lines containing \"xxx\" or \"yyy\"\n"
	    "  \"-xxx\"     hide lines containing \"xxx\"");

	std::bitset<MAX_EDICTS> filterPassedSet{};
	int nPassed = 0;

	for (auto [entIdx, _] : sortedEnts)
	{
		const TrEnt& ent = **entIdx;
		auto objSp = *ent.physSp;

		bool passFilter = !filter.IsActive();
		if (!passFilter)
			passFilter = filter.PassFilter(*ent.classNameIdx);
		if (!passFilter)
			passFilter = filter.PassFilter(*ent.networkClassNameIdx);
		if (!passFilter)
			passFilter = filter.PassFilter(*ent.nameIdx);
		for (size_t i = 0; !passFilter && i < objSp.size(); i++)
			passFilter = filter.PassFilter(*objSp[i]->infoIdx->nameIdx);

		// special case for portals, I accidentally collected them in old versions
		if (!strcmp(*ent.classNameIdx, "prop_portal")) [[unlikely]]
			passFilter = false;

		filterPassedSet[ent.handle.GetEntryIndex()] = passFilter;
		if (passFilter)
			nPassed++;
	}

	ImGui::Text("showing %d/%u entities", nPassed, sortedEnts.size());

	for (auto [entIdx, entTransIdx] : sortedEnts)
	{
		const TrEnt& ent = **entIdx;

		if (!filterPassedSet[ent.handle.GetEntryIndex()])
			continue;

		ImGui::PushID(ent.handle.GetEntryIndex());
		const char* entName = *ent.nameIdx;
		ImGui::BeginGroup();
		if (ImGui::TreeNodeEx("ent_entry",
		                      ImGuiTreeNodeFlags_SpanFullWidth,
		                      "index %d [%s]%s%s%s",
		                      ent.handle.GetEntryIndex(),
		                      *ent.networkClassNameIdx,
		                      *entName ? " \"" : "",
		                      entName,
		                      *entName ? "\"" : ""))
		{
			if (SptImGui::BeginBordered())
			{
				const TrEntTransform& entTrans = **entTransIdx;
				{
					Vector mins{NAN}, maxs{NAN}, pos{NAN};
					QAngle ang{NAN, NAN, NAN};
					if (entTrans.obbIdx.IsValid())
						entTrans.obbIdx->GetMinsMaxs(mins, maxs);
					if (entTrans.obbTransIdx.IsValid())
						entTrans.obbTransIdx->GetPosAng(pos, ang);
					ImGui::Text("OBB mins: " _VEC_FMT, _VEC_UNP(mins));
					ImGui::Text("OBB maxs: " _VEC_FMT, _VEC_UNP(maxs));
					ImGui::Text("server pos: " _VEC_FMT, _VEC_UNP(pos));
					ImGui::Text("server ang: " _VEC_FMT, _VEC_UNP(ang));
				}
				ImGui::Text("serial: %d", ent.handle.GetSerialNumber());
				ImGui::Text("class name: \"%s\"", *ent.classNameIdx);
				ImGui::Text("solid type: %d", ent.m_nSolidType);
				ImGui::Text("solid flags: %d", ent.m_usSolidFlags);
				ImGui::Text("collision group: %d", ent.m_CollisionGroup);

				auto objSp = *ent.physSp;
				if (objSp.empty())
				{
					ImGui::Text("0 physics objects");
				}
				else if (ImGui::TreeNodeEx("phys_objs",
				                           ImGuiTreeNodeFlags_None,
				                           "%" PRIu32 " physics object%s",
				                           objSp.size(),
				                           objSp.size() == 1 ? "" : "s"))
				{
					auto objTransSp = *entTrans.physTransSp;
					Assert(objSp.size() == objTransSp.size());
					for (uint32_t i = 0; i < objSp.size(); i++)
					{
						const TrPhysicsObject& obj = **objSp[i];
						const TrPhysicsObjectInfo& objInfo = **obj.infoIdx;
						const TrPhysMesh_v1& objMesh = **obj.meshIdx;
						const TrTransform& objTrans = **objTransSp[i];

						ImGui::PushID((int)objSp[i]);
						if (SptImGui::BeginBordered())
						{
							ImGui::Text("name: \"%s\"", *objInfo.nameIdx);
							ImGui::Text(
							    "asleep: %d, moveable: %d, trigger: %d, gravity enabled: %d",
							    !!(objInfo.flags & TR_POF_ASLEEP),
							    !!(objInfo.flags & TR_POF_MEOVEABLE),
							    !!(objInfo.flags & TR_POF_IS_TRIGGER),
							    !!(objInfo.flags & TR_POF_GRAVITY_ENABLED));

							Vector pos{NAN};
							QAngle ang{NAN, NAN, NAN};
							objTrans.GetPosAng(pos, ang);
							ImGui::Text("pos: " _VEC_FMT, _VEC_UNP(pos));
							ImGui::Text("ang: " _VEC_FMT, _VEC_UNP(ang));

							if (objMesh.ballRadius > 0)
							{
								ImGui::Text("ball mesh of radius %f",
								            objMesh.ballRadius);
							}
							else
							{
								ImGui::Text("mesh with %" PRIu32 " verts",
								            objMesh.vertIdxSp.n);
							}
							SptImGui::EndBordered();
						}
						ImGui::PopID();
					}
					ImGui::TreePop();
				}
				SptImGui::EndBordered();
				// pad the remaining space with a dummy for hover detection
				auto [_, height] = ImGui::GetItemRectSize();
				ImGui::SameLine();
				ImGui::Dummy(ImVec2{ImGui::GetContentRegionAvail().x, height});
			}
			ImGui::TreePop();
		}
		ImGui::EndGroup();

		if (ImGui::IsItemHovered())
			entCache.hoveredEnt = entIdx;

		ImGui::PopID();
	}
}

void tr_imgui::PortalTabCallback(tr_tick activeTick)
{
	TrShowActiveTick(activeTick);
	auto& tr = TrReadContextScope::Current();

	auto portalSnap = tr.GetAtTick<TrPortalSnapshot>(activeTick);
	auto portalSp = *portalSnap.GetOrDefault().portalsSp;

	if (portalSp.empty())
	{
		ImGui::TextUnformatted("No portals");
		return;
	}

	// a simplified copy of the portal selection widget
	ImGuiTabBarFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_NoHostExtendX
	                              | ImGuiTableFlags_NoKeepColumnsVisible;
	if (ImGui::BeginTable("##portal_select", 11, tableFlags))
	{
		ImGui::TableSetupColumn("index", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("linked", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("color", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("state", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("x", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("y", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("z", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("pitch", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("yaw", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("roll", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("linkage", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableHeadersRow();

		for (auto portalIdx : portalSp)
		{
			auto& portal = portalIdx.GetOrDefault();
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("%d", portal.handle.GetEntryIndex());
			ImGui::TableSetColumnIndex(1);
			if (portal.linkedHandle.IsValid())
				ImGui::Text("%d", portal.linkedHandle.GetEntryIndex());
			else
				ImGui::TextDisabled("NONE");
			ImGui::TableSetColumnIndex(2);
			ImGui::TextColored(portal.isOrange ? ImVec4{1.f, .63f, .13f, 1.f}
			                                   : ImVec4{.25f, .63f, 1.f, 1.f},
			                   portal.isOrange ? "orange" : "blue");
			ImGui::TableSetColumnIndex(3);
			ImGui::TextUnformatted(portal.isOpen ? "open" : (portal.isActivated ? "closed" : "inactive"));
			TrTransform trans = portal.transIdx.GetOrDefault();
			Vector pos;
			QAngle ang;
			trans.GetPosAng(pos, ang);
			for (int i = 0; i < 3; i++)
			{
				ImGui::TableSetColumnIndex(4 + i);
				ImGui::Text("%f", pos[i]);
				ImGui::TableSetColumnIndex(7 + i);
				ImGui::Text("%f", ang[i]);
			}
			ImGui::TableSetColumnIndex(10);
			ImGui::Text("%d", portal.linkageId);
		}

		ImGui::EndTable();
	}
}

#endif
