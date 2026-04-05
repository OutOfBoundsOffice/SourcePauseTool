#include "stdafx.hpp"

#include "mon_feature.hpp"

#ifdef SPT_MONOCLE_FEATURE

#include "spt\features\visualizations\imgui\imgui_interface.hpp"

MonocleWorker::MonocleWorker()
{
	thread = std::thread(&MonocleWorker::WorkerLoop, this);
}

MonocleWorker::~MonocleWorker()
{
	requestStop = true;
	cvWorker.notify_one();
	if (thread.joinable())
		thread.join();
}

mon::Vector MonocleWorker::PixelToWorldCoordinates(WorkerPixel px, const mon::TeleportChainParams* paramsOverride) const
{
	Assert(!!monocleData);
	const mon::TeleportChainParams* params = paramsOverride ? paramsOverride : &monocleData->paramsTemplate;
	const mon::Portal& p = params->EntryPortal();
	// center on each pixel and orient as if we're looking at the portal
	mon::Vector uShift = p.u * (mon::PORTAL_HALF_HEIGHT * ((2 * px.y + 1) / (float)img.size.y - 1));
	mon::Vector rShift = p.r * (mon::PORTAL_HALF_WIDTH * ((2 * px.x + 1) / (float)img.size.x - 1));
	return (p.pos - uShift) - rShift;
}

WorkerPixelData MonocleWorker::DoWorkForPixel(WorkerPixel px,
                                              const mon::TeleportChainParams& paramsTemplate,
                                              mon::TeleportChainResult& result) const
{
	Assert(px.x < img.size.x && px.y < img.size.y);
	mon::TeleportChainParams newParams = paramsTemplate;
	newParams.ent = newParams.ent.WithNewCenter(PixelToWorldCoordinates(px, &paramsTemplate));

	mon::GenerateTeleportChain(newParams, result);

	WorkerPixelData res;
	res.tpExceeded = result.max_tps_exceeded || result.cum_teleports < -32 || result.cum_teleports > 31;
	res.cumTp = (int8_t)result.cum_teleports;
	res.endBehindPortal = EndBehindPortal(newParams, result).value_or(false);

	return res;
}

std::optional<bool> MonocleWorker::EndBehindPortal(const mon::TeleportChainParams& params,
                                                   const mon::TeleportChainResult& result)
{
	if (!result.max_tps_exceeded && (result.cum_teleports == 0 || result.cum_teleports == 1))
	{
		const mon::Portal& p = result.cum_teleports == 0 ? params.EntryPortal() : params.ExitPortal();
		return p.ShouldTeleport(result.ent, false);
	}
	return std::nullopt;
}

void MonocleWorker::RestartWork(std::shared_ptr<WorkerMonocleData> optNewMonocleData, WorkerPixel newImgSize)
{
	Assert(newImgSize.x > 0 && newImgSize.y > 0);
	PauseWorker();
	monocleData = std::move(optNewMonocleData);
	if (!img.pxls || img.size != newImgSize)
	{
		img.pxls = std::make_unique_for_overwrite<WorkerPixelData[]>(newImgSize.x * newImgSize.y);
		img.size = newImgSize;
	}
	img.processedRows = 0;
	requestPause = false;
	cvWorker.notify_one();
	SetupTexture();
	if (monocleData)
		RecomputeEffectiveTpSpace();
}

void MonocleWorker::SetupTexture()
{
	MarkTextureDirty();

	bool recreateTex = !tex.texture;
	if (!recreateTex)
	{
		D3DSURFACE_DESC desc;
		tex.texture->GetLevelDesc(0, &desc);
		recreateTex = desc.Width != img.size.x || desc.Height != img.size.y;
	}

	if (recreateTex)
	{
		HRESULT hr = SptImGui::GetDx9Device()->CreateTexture(img.size.x,
		                                                     img.size.y,
		                                                     1,
		                                                     D3DUSAGE_DYNAMIC,
		                                                     D3DFMT_A8R8G8B8,
		                                                     D3DPOOL_DEFAULT,
		                                                     &tex.texture,
		                                                     nullptr);
		if (hr != D3D_OK)
		{
			Assert(0);
			return;
		}
	}

	D3DLOCKED_RECT lRect;
	HRESULT hr = tex.texture->LockRect(0, &lRect, nullptr, D3DLOCK_DISCARD);
	if (hr != D3D_OK)
	{
		Assert(0);
		return;
	}

	for (UINT y = 0; y < img.size.y; y++)
	{
		d3d9_argb* row = (d3d9_argb*)((uint8_t*)lRect.pBits + y * lRect.Pitch);
		std::fill_n(row, img.size.x, d3d9_argb{0, 0, 0, 0});
	}

	tex.texture->UnlockRect(0);
}

void MonocleWorker::PauseWorker()
{
	Assert(!requestStop && thread.joinable());
	requestPause = true;
	std::unique_lock lk(mtx);
	cvMain.wait(lk, [this]() { return paused; });
}

ComPtr<IDirect3DTexture9> MonocleWorker::UpdateAndGetTexture()
{
	if (!tex.texture)
	{
		Assert(0);
		return nullptr;
	}

	size_t nProcessed = img.processedRows.load(std::memory_order_acquire);
	if (tex.nRowsUploaded < nProcessed)
	{
		RECT rect{
		    .left = 0,
		    .top = (LONG)tex.nRowsUploaded,
		    .right = (LONG)img.size.x,
		    .bottom = (LONG)nProcessed,
		};
		D3DLOCKED_RECT lRect;
		HRESULT hr = tex.texture->LockRect(0, &lRect, &rect, 0);
		if (hr != D3D_OK)
		{
			Assert(0);
			return tex.texture.Get();
		}

		for (UINT y = 0; y < nProcessed - tex.nRowsUploaded; y++)
		{
			d3d9_argb* texRow = (d3d9_argb*)((uint8_t*)lRect.pBits + y * lRect.Pitch);
			WorkerPixelData* workRow = img.pxls.get() + (y + tex.nRowsUploaded) * img.size.x;

			for (UINT x = 0; x < img.size.x; x++)
			{
				WorkerPixelData* wrPxl = workRow + x;
				d3d9_argb* p = texRow + x;
				*p = MonocleImageColors[wrPxl->ToColor()];
			}
		}

		tex.nRowsUploaded = nProcessed;
		tex.texture->UnlockRect(0);
	}

	return tex.texture.Get();
}

void MonocleWorker::WorkerLoop()
{
	mon::MonocleFloatingPointScope scope{};

	for (;;)
	{
		{
			std::unique_lock lk(mtx);
			paused = true;
			cvMain.notify_one();
			cvWorker.wait(lk,
			              [this]()
			              {
				              if (requestStop)
					              return true;
				              if (!monocleData || requestPause)
					              return false;
				              return img.processedRows.load(std::memory_order_acquire) < img.size.y;
			              });
			paused = false;
		}
		if (requestStop)
			return;

		// fill in 1 row

		uint16_t y = img.processedRows.load(std::memory_order_acquire);
		Assert(!!monocleData && y < img.size.y);

		const mon::TeleportChainParams& params = monocleData->paramsTemplate;
		mon::TeleportChainResult result;

		for (uint16_t x = 0; x < img.size.x; x++)
			img.pxls[y * img.size.x + x] = DoWorkForPixel({x, y}, params, result);

		img.processedRows.store(y + 1, std::memory_order_release);
	}
}

void MonocleWorker::RecomputeEffectiveTpSpace()
{
	auto& ent = monocleData->paramsTemplate.ent;
	auto& p = monocleData->paramsTemplate.EntryPortal();

	// first, we build 4 projected (2D) offsets from each corner in the portal's local 3D space (+z is up, +y is left)
	std::array<Vector2D, 4> cornerOffsetsLocal;

	if (ent.is_player)
	{
		/*
		* The player will always be teleported when their center is on the portal plane. To figure
		* out the effective teleport space:
		* - slice the player using the portal plane to create a 2D polygon
		* - create an AABB of that polygon
		* - "subtract" that AABB from the portal rectangle
		*/

		matrix3x4_t portalToWorld;
		AngleIMatrix(*(QAngle*)&p.ang, *(Vector*)&p.pos, portalToWorld);

		// create an AABB centered at the portal
		mon::Entity centeredEnt = ent.WithNewCenter(p.pos);
		mon::Vector mins = centeredEnt.GetWorldMins();
		mon::Vector maxs = centeredEnt.GetWorldMaxs();

		// get a cross section of the player sliced by the portal plane (world space)
		std::array<Vector, 10> edgeIntersectionsWorldSpace;
		size_t intersectionCount = 0;

		auto checkPlaneIntersectLine = [&](const Vector& a, const Vector& b)
		{
			const VPlane& portalPlane = *(VPlane*)&p.plane;
			float aDotN = a.Dot(portalPlane.m_Normal);
			float bDotN = b.Dot(portalPlane.m_Normal);
			// t is the fraction at which the line segment a->b intersects the portal plane
			float t = (portalPlane.m_Dist - aDotN) / (bDotN - aDotN);
			// because of this epsilon, we may sometimes get false positives and include more edges than necessary (up to 10)
			if (t >= -0.0001f && t <= 1.0001f)
				edgeIntersectionsWorldSpace[intersectionCount++] = Lerp(t, a, b);
		};

		// iterate over all player AABB edges

		// edges from the mins corner
		checkPlaneIntersectLine(*(Vector*)&mins, {maxs.x, mins.y, mins.z});
		checkPlaneIntersectLine(*(Vector*)&mins, {mins.x, maxs.y, mins.z});
		checkPlaneIntersectLine(*(Vector*)&mins, {mins.x, mins.y, maxs.z});

		// the other 9 edges :)
		for (size_t r = 0; r < 3; r++)
		{
			Vector from = *(Vector*)&maxs;
			from[r] = mins[r];
			for (size_t flipAx = 0; flipAx < 3; flipAx++)
			{
				Vector to = from;
				to[flipAx] = r == flipAx ? maxs[flipAx] : mins[flipAx];
				checkPlaneIntersectLine(from, to);
			}
		}

		// convert the intersections to a local space AABB (+z is up, +y is left)
		Vector localMins(666, 666, 666);
		Vector localMaxs(-666, -666, -666);

		for (size_t i = 0; i < intersectionCount; i++)
		{
			Vector intersectionLocal;
			VectorTransform(edgeIntersectionsWorldSpace[i], portalToWorld, intersectionLocal);
			VectorMin(intersectionLocal, localMins, localMins);
			VectorMax(intersectionLocal, localMaxs, localMaxs);
		}

		cornerOffsetsLocal[0] = {-localMins.y, -localMaxs.z};
		cornerOffsetsLocal[1] = {-localMaxs.y, -localMaxs.z};
		cornerOffsetsLocal[2] = {-localMaxs.y, -localMins.z};
		cornerOffsetsLocal[3] = {-localMins.y, -localMins.z};
	}
	else
	{
		// ball is easy - fixed distance away from each corner
		float r = ent.ball.radius;
		cornerOffsetsLocal = {
		    Vector2D{r, -r},
		    Vector2D{-r, -r},
		    Vector2D{-r, r},
		    Vector2D{r, r},
		};
	}

	// convert the projected corner offsets into pixel space and add to the actual corner position (+x is right, +y is down)
	for (size_t i = 0; i < img.effectiveTpSpace.size(); i++)
	{
		img.effectiveTpSpace[i] = Vector2D{
		    i == 0 || i == 3 ? 0.5f : img.size.x - 0.5f,
		    i == 0 || i == 1 ? 0.5f : img.size.y - 0.5f,
		};
		img.effectiveTpSpace[i] += Vector2D{
		    cornerOffsetsLocal[i].x * 0.5f / mon::PORTAL_HALF_WIDTH * img.size.x,
		    -cornerOffsetsLocal[i].y * 0.5f / mon::PORTAL_HALF_HEIGHT * img.size.y,
		};
	}
}

#endif
