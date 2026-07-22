#include "stdafx.hpp"

#include "renderer\mesh_renderer.hpp"

#ifdef SPT_MESH_RENDERING_ENABLED

#include <clocale>
#include <chrono>

#include "spt\utils\math.hpp"
#include "spt\feature.hpp"
#include "spt\utils\signals.hpp"
#include "spt\utils\interfaces.hpp"
#include "mesh_test_data.hpp"
#include "spt\flatbuffers_schemas\fb_forward_declare.hpp"
#include "spt\flatbuffers_schemas\gen\mesh_generated.h"

// purpose: serves as way to visually test the mesh builder as well as show how to use some of its features

ConVar y_spt_draw_mesh_examples("y_spt_draw_mesh_examples",
                                "0",
                                FCVAR_DONTRECORD,
                                "Draw a bunch of example meshes near the origin.");

static std::vector<class BaseMeshRenderTest*> tests;

class BaseMeshRenderTest
{
public:
	const Vector testPos;
	const char* name;

	explicit BaseMeshRenderTest(const Vector& pos, const char* name) : testPos(pos), name(name)
	{
		tests.push_back(this);
	}

	virtual void TestFunc(MeshRendererDelegate& mr) = 0;

	virtual void DrawName(MeshRendererDelegate& mr)
	{
		if (interfaces::debugOverlay)
			interfaces::debugOverlay->AddTextOverlay(testPos + Vector{0, 0, 150}, 0, name);
	}
};

class MeshTestFeature : public FeatureWrapper<MeshTestFeature>
{
protected:
	virtual void LoadFeature()
	{
		if (spt_meshRenderer.signal.Works)
		{
			spt_meshRenderer.signal.Connect(this, &MeshTestFeature::OnMeshRenderSignal);
			InitConcommandBase(y_spt_draw_mesh_examples);
		}
	};

	void OnMeshRenderSignal(MeshRendererDelegate& mr)
	{
		float lastTime = time;
		time = interfaces::engine_tool->HostTime();
		if (timeSinceLast < 0)
			timeSinceLast = 0; // init
		else
			timeSinceLast = time - lastTime;

		if (!y_spt_draw_mesh_examples.GetBool())
			return;
		for (auto testCase : tests)
		{
			testCase->DrawName(mr);
			testCase->TestFunc(mr);
		}
	}

public:
	float time;
	float timeSinceLast = -666;
};

static MeshTestFeature testFeature;

#define _TEST_CASE(name, position, counter) \
	struct CONCATENATE(_Test, counter) : BaseMeshRenderTest \
	{ \
		using BaseMeshRenderTest::BaseMeshRenderTest; \
		virtual void TestFunc(MeshRendererDelegate& mr) override; \
	} static CONCATENATE(_inst, counter){position, name}; \
	void CONCATENATE(_Test, counter)::TestFunc(MeshRendererDelegate& mr)

#define TEST_CASE(name, position) _TEST_CASE(name, position, __COUNTER__)

TEST_CASE("AddLine()", Vector(0, 0, 0))
{
	RENDER_DYNAMIC(mr,
	               mb.AddLine(testPos + Vector(-70, -70, -80),
	                          testPos + Vector(70, 70, 80),
	                          {{255, 0, 255, 100}}););
}

const Vector lineTestVerts[] = {{0, 0, 80}, {-70, 70, 80}, {-70, 70, -80}, {0, 0, -80}, {70, 70, -80}, {70, 70, 80}};
const int numLineTestVerts = sizeof(lineTestVerts) / sizeof(Vector);

TEST_CASE("AddLines()", Vector(200, 0, 0))
{
	Vector v[numLineTestVerts];
	for (int i = 0; i < numLineTestVerts; i++)
		v[i] = lineTestVerts[i] + testPos;
	RENDER_DYNAMIC(mr, mb.AddLines(v, numLineTestVerts / 2, {{255, 120, 120, 255}}););
}

TEST_CASE("AddLineStrip(loop=false)", Vector(400, 0, 0))
{
	Vector v[numLineTestVerts];
	for (int i = 0; i < numLineTestVerts; i++)
		v[i] = lineTestVerts[i] + testPos;
	RENDER_DYNAMIC(mr, mb.AddLineStrip(v, numLineTestVerts, false, {{120, 255, 120, 255}}););
}

TEST_CASE("AddLineStrip(loop=true)", Vector(600, 0, 0))
{
	Vector v[numLineTestVerts];
	for (int i = 0; i < numLineTestVerts; i++)
		v[i] = lineTestVerts[i] + testPos;
	RENDER_DYNAMIC(mr, mb.AddLineStrip(v, numLineTestVerts, true, {{120, 120, 255, 255}}););
}

TEST_CASE("AddPolygon()", Vector(800, 0, 0))
{
	// clang-format off
	Vector v1[] = {testPos + Vector{-70, -70, 80}, testPos + Vector{0, 70, 80}, testPos + Vector{70, -70, 80}};
	Vector v2[] = {testPos + Vector{-70, -70, 30}, testPos + Vector{0, 70, 30}, testPos + Vector{80, 30, 30}, testPos + Vector{70, -70, 30}};
	Vector v3[] = {testPos + Vector{-70, -70, -30}, testPos + Vector{-70, 30, -30}, testPos + Vector{0, 70, -30}, testPos + Vector{80, 30, -30}, testPos + Vector{70, -70, -30}};
	// clang-format on

	// Add top to bottom and check if they get sorted properly:
	// looking through the top you should see mostly green (not yellow) and not be able to see the bottom wireframe
	RENDER_DYNAMIC(mr, mb.AddPolygon(v1, ARRAYSIZE(v1), {{50, 255, 50, 220}, {255, 50, 255, 255}}););
	RENDER_DYNAMIC(mr, mb.AddPolygon(v2, ARRAYSIZE(v2), {C_OUTLINE(255, 50, 50, 100)}););
	RENDER_DYNAMIC(mr, mb.AddPolygon(v3, ARRAYSIZE(v3), {{150, 150, 150, 255}, {0, 0, 255, 250}}););
}

TEST_CASE("AddCircle()", Vector(1000, 0, 0))
{
	Vector dir(4, 2, 13);
	QAngle ang;
	VectorAngles(dir, ang);
	for (int i = 10; i >= 0; i--)
	{
		RENDER_DYNAMIC(mr, {
			mb.AddCircle(testPos + Vector(-20, 0, -30) + dir * i,
			             ang,
			             20 + 8 * i,
			             3 * (i + 1),
			             {C_OUTLINE((byte)(250 - i * 20), 200, (byte)(i * 20), 50)});
		});
	}
	// jwst tribute
	RENDER_DYNAMIC(mr, {
		Vector center = testPos + Vector(0, 200, 100);
		for (int i = 0; i < 6; i++)
		{
			float s;
			float c;
			SinCos(M_PI_F / 3 * i, &s, &c);
			int jMax = 10;
			for (int j = 0; j <= jMax; j++)
			{
				Vector pos = center + Vector(s, 0, c) * 50 * (.025 * (j - jMax) + 1);
				QAngle ang(0, 90, 30);
				mb.AddCircle(pos, ang, 2 + j * 24.5 / jMax, 6, {C_WIRE(200, 255, 0, 255)});
			}
		}
	});
}

TEST_CASE("AddTris()", Vector(1200, 0, 0))
{
	float c30, s30;
	SinCos(DEG2RAD(30), &s30, &c30);
	float triRad = 50;
	float topZ = 60;
	// clang-format off
	const Vector v[] = {{0, triRad, topZ}, {triRad * c30, -triRad * s30, topZ}, {-triRad * c30, -triRad * s30, topZ},
	                    v[1], v[0], (v[0] + v[1]) / 2 - Vector(0, 0, triRad * (1 + s30)),
	                    v[2], v[1], (v[1] + v[2]) / 2 - Vector(0, 0, triRad * (1 + s30)),
	                    v[0], v[2], (v[0] + v[2]) / 2 - Vector(0, 0, triRad * (1 + s30))};
	// clang-format on
	const int numVerts = sizeof(v) / sizeof(Vector);
	Vector vNew[numVerts];
	for (int i = 0; i < numVerts; i++)
		vNew[i] = v[i] + testPos;
	RENDER_DYNAMIC(mr, mb.AddTris(vNew, numVerts / 3, {C_OUTLINE(50, 150, 200, 30)}););
}

TEST_CASE("AddQuad()", Vector(1400, 0, 0))
{
	// naive game of life implementation - alternate between two boards

	const size_t size = 35; // one row/column on each side for padding
	const int maxMinigameTicks = 250;
	const float secPerMinigameTick = .09;

	static std::vector<bool> state0(size * size);
	static std::vector<bool> state1(size * size);
	static float remainderTime = 0;
	static int minigameTick = maxMinigameTicks;
	remainderTime += testFeature.timeSinceLast;

	if (minigameTick >= maxMinigameTicks)
	{
		// randomize board
		for (size_t y = 1; y < size - 1; y++)
			for (size_t x = 1; x < size - 1; x++)
				state0[y * size + x] = rand() % 2;
		minigameTick = 0;
	}

	// update board state
	while (remainderTime >= secPerMinigameTick && minigameTick < maxMinigameTicks)
	{
		remainderTime -= secPerMinigameTick;
		minigameTick++;

		auto& curBoard = minigameTick % 2 ? state0 : state1;
		auto& newBoard = minigameTick % 2 ? state1 : state0;
		int numUpdated = 0;
		for (size_t y = 1; y < size - 1; y++)
		{
			for (size_t x = 1; x < size - 1; x++)
			{
				// clang-format off
				int numSurrounding =
					curBoard[(y + 1) * size + (x - 1)] + curBoard[(y + 1) * size + x] + curBoard[(y + 1) * size + (x + 1)] +
					curBoard[(y + 0) * size + (x - 1)] + 0                            + curBoard[(y + 0) * size + (x + 1)] +
					curBoard[(y - 1) * size + (x - 1)] + curBoard[(y - 1) * size + x] + curBoard[(y - 1) * size + (x + 1)];
				// clang-format on
				size_t curCellIdx = y * size + x;
				bool curCell = curBoard[curCellIdx];
				newBoard[curCellIdx] =
				    (curCell && (numSurrounding == 2 || numSurrounding == 3)) // stay alive
				    || (!curCell && numSurrounding == 3);                     // reproduce
				numUpdated += curCell != newBoard[curCellIdx];
			}
		}
		if (numUpdated < 10 && minigameTick + 2 <= maxMinigameTicks) // naive check to speed up simulation
			minigameTick += 2;
	}
	mr.DrawMesh(spt_meshBuilder.CreateDynamicMesh(
	    [&](MeshBuilderDelegate& mb)
	    {
		    color32 cLerp = color32RgbLerp(color32{50, 50, 200, 255},
		                                   color32{150, 0, 0, 255},
		                                   (float)minigameTick / maxMinigameTicks);

		    auto& curBoard = minigameTick % 2 ? state1 : state0;
		    const float tileSize = 4;
		    Vector boardCorner = testPos - Vector(tileSize * size / 2, tileSize * size / 2, 0);
		    for (size_t y = 1; y < size - 1; y++)
		    {
			    for (size_t x = 1; x < size - 1; x++)
			    {
				    if (!curBoard[y * size + x])
					    continue;
				    Vector tileCorner = boardCorner + Vector(x * tileSize, y * tileSize, 0);
				    mb.AddQuad(tileCorner,
				               tileCorner + Vector(0, tileSize, 0),
				               tileCorner + Vector(tileSize, tileSize, 0),
				               tileCorner + Vector(tileSize, 0, 0),
				               {cLerp, {150, 150, 150, 255}, true, true, WD_BOTH});
			    }
		    }
		    // outline board
		    mb.AddQuad(boardCorner,
		               boardCorner + Vector(0, tileSize * size, 0),
		               boardCorner + Vector(tileSize * size, tileSize * size, 0),
		               boardCorner + Vector(tileSize * size, 0, 0),
		               {C_WIRE(50, 50, 50, 255)});
	    }));
}

TEST_CASE("AddBox()", Vector(1600, 0, 0))
{
	const Vector mins(-16, -16, 0);
	const Vector maxs(16, 16, 72);
	const Vector minsInner = mins * 0.7;
	const Vector maxsInner = {16 * .7, 16 * .7, 72};
	RENDER_DYNAMIC(mr, {
		mb.AddBox(testPos, minsInner, maxsInner, vec3_angle, {C_OUTLINE(255, 0, 0, 100)});
		mb.AddBox(testPos, mins, maxs, vec3_angle, {C_WIRE(255, 100, 0, 255)});
		mb.AddBox(testPos, mins, maxs, {0, 0, 20}, {C_OUTLINE(255, 255, 0, 16)});
	});
}

TEST_CASE("AddSphere()", Vector(1800, 0, 0))
{
	RENDER_DYNAMIC(mr, mb.AddSphere(testPos + Vector(0, 100, 20), 50, 8, {C_FACE(150, 100, 255, 50)}););
	RENDER_DYNAMIC(mr, mb.AddSphere(testPos + Vector(60, 0, 20), 50, 4, {C_OUTLINE(0, 200, 200, 50)}););
	RENDER_DYNAMIC(mr, mb.AddSphere(testPos + Vector(-60, 0, 20), 50, 9, {{20, 50, 80, 255}, {180, 99, 30, 255}}););
}

TEST_CASE("AddSweptBox()", Vector(2000, 0, 0))
{
	/*
	* The sweep may be drawn differently if it has a zero component in any of the axes and
	* the index math for swept boxes is very non-trivial, so check all possible combinations
	* of the components of the sweep being 0. Also check cases where the sweep is small -
	* the may lead to weird cases where the sweep may overlap the start/end in unexpected ways.
	*/

	SweptBoxColor color{C_OUTLINE(0, 255, 0, 20), C_OUTLINE(150, 150, 150, 20), C_OUTLINE(255, 0, 0, 20)};
	const Vector mins(-5, -7, -9);
	const Vector maxs(7, 9, 11);
	const float sweepMags[] = {10, 25}; // test small and large sweeps
	const float xOffs[] = {-50, 50};
	const float ySpacing = 90;

	{
		// no sweep
		Vector start = testPos + Vector(0, 0, 60);
		RENDER_DYNAMIC(mr, mb.AddSweptBox(start, start + Vector(0.001), mins, maxs, color););
	}

	for (int i = 0; i < 2; i++)
	{
		// test all (24!!!) edge cases
		int yOff = 0;
		for (int excludeAxIdx = 0; excludeAxIdx < 3; excludeAxIdx++)
		{
			int axIdx0 = (excludeAxIdx + 1) % 3;
			int axIdx1 = (excludeAxIdx + 2) % 3;
			for (int ax0 = 0; ax0 < 2; ax0++)
			{
				for (int ax1 = 0; ax1 < 2; ax1++)
				{
					for (int j = 0; j < 2; j++)
					{
						Vector start = testPos + Vector(xOffs[i], ++yOff * ySpacing, 0);
						Vector off;
						off[excludeAxIdx] = 0;
						off[axIdx0] = (ax0 - .5) * (j ? 1 : .2);
						off[axIdx1] = (ax1 - .5) * (j ? .2 : 1);
						VectorNormalize(off);
						Vector end = start + off * sweepMags[i];
						RENDER_DYNAMIC(mr, mb.AddSweptBox(start, end, mins, maxs, color););
					}
				}
			}
		}
		// test all corner cases
		yOff = 0;
		for (int ax0 = 0; ax0 < 2; ax0++)
		{
			for (int ax1 = 0; ax1 < 2; ax1++)
			{
				for (int ax2 = 0; ax2 < 2; ax2++)
				{
					Vector start = testPos + Vector(xOffs[i], ++yOff * ySpacing, 60);
					Vector off(ax0 - .5, ax1 - .5, ax2 - .5);
					VectorNormalize(off);
					Vector end = start + off * sweepMags[i];
					RENDER_DYNAMIC(mr, mb.AddSweptBox(start, end, mins, maxs, color););
				}
			}
		}
		// test all face cases
		yOff = 0;
		for (int ax = 0; ax < 3; ax++)
		{
			for (int reflect = 0; reflect < 2; reflect++)
			{
				Vector start = testPos + Vector(xOffs[i], ++yOff * ySpacing, 120);
				Vector off(0);
				off[ax] = reflect - .5;
				VectorNormalize(off);
				Vector end = start + off * sweepMags[i];
				RENDER_DYNAMIC(mr, mb.AddSweptBox(start, end, mins, maxs, color););
			}
		}
	}
}

TEST_CASE("AddCone()", Vector(2200, 0, 0))
{
	{
		const Vector org = testPos + Vector(-50, 0, 0);
		const Vector tipOff(30, 50, 70);
		QAngle ang;
		VectorAngles(tipOff, ang);
		float h = tipOff.Length();
		RENDER_DYNAMIC(mr, mb.AddCone(org, ang, h, 20, 20, false, {C_OUTLINE(255, 255, 50, 20)}););
	}
	{
		const Vector org = testPos + Vector(50, 0, 0);
		const Vector tipOff(-30, -50, -70);
		QAngle ang;
		VectorAngles(tipOff, ang);
		float h = tipOff.Length();
		RENDER_DYNAMIC(mr, mb.AddCone(org, ang, h, 20, 5, true, {C_OUTLINE(255, 50, 50, 20)}););
	}
}

TEST_CASE("AddCylinder()", Vector(2400, 0, 0))
{
	RENDER_DYNAMIC(mr, {
		mb.AddCylinder(testPos, {0, 0, 0}, 20, 10, 5, true, true, {C_OUTLINE(255, 150, 0, 40)});
	});
	Vector org = testPos + Vector(0, 100, 0);
	RENDER_DYNAMIC(mr, {
		mb.AddCylinder(org, {0, 180, 0}, 20, 10, 10, false, true, {C_OUTLINE(255, 100, 0, 30)});
	});
	org += Vector(0, 100, 0);
	RENDER_DYNAMIC(mr, {
		mb.AddCylinder(org, {-80, 90, 0}, 20, 10, 15, true, false, {C_OUTLINE(255, 50, 0, 20)});
	});
}

TEST_CASE("AddArrow3D()", Vector(2600, 0, 0))
{
	const Vector target = testPos + Vector(0, 100, 50);
	RENDER_DYNAMIC(mr, mb.AddCross(target, 7, {{255, 0, 0, 255}}););
	for (int i = 0; i < 4; i++)
	{
		for (int k = 0; k < 3; k++)
		{
			Vector start = testPos + Vector((i - 1.5) * 25, k * 75, k * 30);
			unsigned char g = (unsigned char)(1.f / (1 + start.DistToSqr(target) / 2000) * 255);
			unsigned char b = (unsigned char)(1.f / (1 + start.DistToSqr(target) / 3000) * 255);
			RENDER_DYNAMIC(mr, {
				mb.AddArrow3D(start, target, ArrowParams{7, 20.f, 1.5f}, {C_OUTLINE(99, g, b, 20)});
			});
		}
	}
}

TEST_CASE("Timmy", Vector(0, -300, 0))
{
	static StaticMesh timmyMesh;
	if (!timmyMesh.Valid())
	{
		const Vector mins = {-20, -20, -20};
		timmyMesh = MB_STATIC(
		    mb.AddBox(vec3_origin, mins, -mins, vec3_angle, {{100, 100, 255, 255}, {255, 0, 255, 255}}););
	}

	static QAngle ang(0, 0, 0);
	ang[0] = fmod(ang[0] + 89 * testFeature.timeSinceLast, 360);
	ang[1] = fmod(ang[1] + 199 * testFeature.timeSinceLast, 360);
	ang[2] = fmod(ang[2] + 317 * testFeature.timeSinceLast, 360);

	mr.DrawMesh(timmyMesh,
	            [this](const CallbackInfoIn& infoIn, CallbackInfoOut& infoOut)
	            {
		            float scale = sin(testFeature.time) / 4 + 1;
		            matrix3x4_t tmpMat;
		            SetScaleMatrix(scale, infoOut.mat);
		            AngleMatrix(ang, testPos, tmpMat);
		            MatrixMultiply(tmpMat, infoOut.mat, infoOut.mat);
		            infoOut.colorModulate.a = (sin(testFeature.time) + 1) / 2.f * 255;
		            // timmy will be more yellow through portals
		            if (infoIn.portalRenderDepth >= 1)
			            infoOut.colorModulate.b = 0;
		            // timmy will be more green on the overlay
		            if (infoIn.inOverlayView)
			            infoOut.colorModulate.r = 0;
	            });
}

TEST_CASE("Lorenz Attractor", Vector(200, -300, 0))
{
	// keep the last 2500 positions and draw them every frame
	static std::vector<Vector> verts = {{1, 1, 1}};
	const size_t maxVerts = 2500;
	static size_t lastIdx = 0;
	// viridis colormap from python pyplot, the colors are linearly interpolated between these
	static std::vector<color32> colors = {{253, 231, 37, 255},
	                                      {94, 201, 98, 200},
	                                      {33, 145, 140, 150},
	                                      {59, 82, 139, 100},
	                                      {68, 1, 84, 0}};
	const float sigma = 10, beta = 8.f / 3, rho = 28, timestep = 0.007;

	const float secPerStep = 0.004;
	static float remainder = 0;
	remainder += testFeature.timeSinceLast;

	// calc next positions
	while (remainder >= secPerStep)
	{
		remainder -= secPerStep;
		Vector& vPrev = verts[lastIdx++]; // most recent point
		Vector delta{
		    sigma * (vPrev.y - vPrev.x),
		    vPrev.x * (rho - vPrev.z) - vPrev.y,
		    vPrev.x * vPrev.y - beta * vPrev.z,
		};
		Vector vNext = vPrev + delta * timestep; // next point

		if (verts.size() < maxVerts)
			verts.push_back(vNext);
		else
			verts[lastIdx %= maxVerts] = vNext;
	}

	mr.DrawMesh(spt_meshBuilder.CreateDynamicMesh(
	                [&](MeshBuilderDelegate& mb)
	                {
		                // draw line segments between each point
		                // TODO this is something that can be optimized if we expose the internal buffers
		                Vector* prev = nullptr;
		                for (size_t i = 0; i < verts.size(); i++)
		                {
			                // draw from most to last recent
			                size_t idx = (maxVerts + lastIdx - i) % maxVerts;
			                Vector* cur = &verts[idx];
			                if (i > 0)
			                {
				                // figure out which two colors to lerp between
				                float colorIdx = (float)i / ((float)maxVerts / (colors.size() - 1));
				                color32 cLerp = color32RgbLerp(colors[Floor2Int(colorIdx)],
				                                               colors[Ceil2Int(colorIdx)],
				                                               colorIdx - Floor2Int(colorIdx));
				                mb.AddLine(*prev, *cur, cLerp);
			                }
			                prev = cur;
		                }
		                // put ball at most recent position (draw last so it draws on top)
		                mb.AddSphere(verts[lastIdx], 0.6, 2, {C_FACE(200, 255, 200, 255)});
	                }),
	            [this](const CallbackInfoIn& infoIn, CallbackInfoOut& infoOut)
	            {
		            // the Lorenz Attractor chills near the origin and is quite small, so scale it up and put at test case pos
		            SetScaleMatrix(3, infoOut.mat);
		            PositionMatrix(testPos, infoOut.mat);
	            });
}

TEST_CASE("Reusing static/dynamic meshes", Vector(400, -300, 0))
{
	// returns a create func given a color
	auto coloredCreateFunc = [](const ShapeColor& c) {
		return [&](MeshBuilderDelegate& mb) {
			mb.AddBox(vec3_origin, {-10, -10, 0}, {10, 10, 50}, vec3_angle, c);
	};
	};

	// create once
	static StaticMesh staticMesh;
	if (!staticMesh.Valid())
		staticMesh = spt_meshBuilder.CreateStaticMesh(coloredCreateFunc({C_OUTLINE(0, 0, 255, 20)}));

	// recreate every frame
	DynamicMesh dynamicMesh = spt_meshBuilder.CreateDynamicMesh(coloredCreateFunc({C_OUTLINE(255, 0, 0, 20)}));

	for (int i = 0; i < 20; i++)
	{
		auto callbackFunc = [this, i](const CallbackInfoIn& infoIn, CallbackInfoOut& infoOut)
		{
			float ts = sin(testFeature.time * 8 + i / 2.f);
			Vector scale(1 + ts * .2f, 0, 1 - ts * .2f);
			SetScaleMatrix(1 + ts * .2f, 1, 1 - ts * .2f, infoOut.mat);
			MatrixSetColumn(testPos + Vector(0, -i * 25, 0), 3, infoOut.mat);
		};
		if (i % 2)
			mr.DrawMesh(staticMesh, callbackFunc);
		else
			mr.DrawMesh(dynamicMesh, callbackFunc);
	}
}

TEST_CASE("AddCPolyhedron", Vector(600, -300, 0))
{
	static std::vector<VPlane> planes;
	planes.clear();

	// carve out a cube
	const float cubeRad = 40;
	for (int ax = 0; ax < 3; ax++)
	{
		for (int side = 0; side < 2; side++)
		{
			Vector n{0};
			n[ax] = side * 2 - 1;
			planes.emplace_back(n, cubeRad);
		}
	}

	// carve out a dodecahedron
	float dodecaRad = 50 + sin(testFeature.time) * 20;
	planes.emplace_back(Vector{0, 0, 1}, dodecaRad);
	planes.emplace_back(Vector{0, 0, -1}, dodecaRad);
	for (int i = 0; i < 2; i++)
	{
		float z = cos((i + 1) * M_PI_F / 3);
		float angOff = i * M_PI_F / 5;
		for (int j = 0; j < 5; j++)
		{
			float x, y;
			SinCos(j * M_PI_F / 2.5f + angOff, &x, &y);
			Vector n{x, y, z};
			planes.emplace_back(n * 0.8944272f, dodecaRad);
		}
	}

	CPolyhedron* polyhedron = GeneratePolyhedronFromPlanes((float*)planes.data(), planes.size(), 0.0001f, true);

	mr.DrawMesh(spt_meshBuilder.CreateDynamicMesh(
	                [&](MeshBuilderDelegate& mb) { mb.AddCPolyhedron(polyhedron, {C_OUTLINE(200, 150, 50, 20)}); }),
	            [this](auto&, CallbackInfoOut& infoOut) { MatrixSetColumn(testPos, 3, infoOut.mat); });

	polyhedron->Release();
}

TEST_CASE("AddEllipse()", Vector(800, -300, 0))
{
	Vector dir(4, 2, 13);
	QAngle ang;
	VectorAngles(dir, ang);
	float radius = 100.0f;
	for (float i = 8.0; i >= 2.0f; i -= 0.5f)
	{
		// let's make these guys not ztest
		RENDER_DYNAMIC(mr, {
			mb.AddEllipse(testPos + Vector(-20, 0, -30) + dir * i,
			              ang,
			              radius / i,
			              radius / (10 - i),
			              32,
			              {C_OUTLINE((byte)(250 - i * 40), 200, (byte)(i * 40), 50), false, false});
		});
	}
}

// this is just to ensure I don't rely on meshes not being empty
TEST_CASE("Empty meshes", Vector(1000, -300, 0))
{
	static StaticMesh staticMesh;
	if (!staticMesh.Valid())
		staticMesh = spt_meshBuilder.CreateStaticMesh([](auto&) {});
	DynamicMesh dynamicMesh = spt_meshBuilder.CreateDynamicMesh([](auto&) {});
	mr.DrawMesh(staticMesh);
	mr.DrawMesh(dynamicMesh);
}

TEST_CASE("Testing winding direction", Vector(1200, -300, 0))
{
	/*
	* There's 3 parts to this test:
	* - test all 3 winding directions for all primitives
	* - test that the index math is correct when the indices don't start at 0
	* - test negative shape sizes, e.g. a cylinder with a negative height should *not* be inside out
	* It's not strictly necessary to test all mesh builder functions because internally some defer to others,
	* e.g. AddTri() & AddTris() both defer to _AddTris().
	*/

	mr.DrawMesh(spt_meshBuilder.CreateDynamicMesh(
	    [this](MeshBuilderDelegate& mb)
	    {
		    ShapeColor colors[] = {
		        {C_OUTLINE(0, 255, 0, 20), true, true, WD_CW},
		        {C_OUTLINE(255, 0, 0, 20), true, true, WD_CCW},
		        {C_OUTLINE(255, 255, 0, 20), true, true, WD_BOTH},
		    };

		    const float ySpacing = -40;
		    const float shapeRadius = fabs(ySpacing) / 3.f;

		    for (int i = 0; i < 3; i++)
		    {
			    ShapeColor color = colors[i];
			    Vector pos = testPos;
			    pos.x += (i - 1) * 50;

			    mb.AddTri(pos, pos + Vector{20, -20, 0}, pos + Vector{-20, -20, 0}, color);

			    pos.y += ySpacing;

			    mb.AddQuad(pos + Vector{-10, 10, 0},
			               pos + Vector{10, 10, 0},
			               pos + Vector{10, -10, 0},
			               pos + Vector{-10, -10, 0},
			               color);

			    pos.y += ySpacing;

			    mb.AddEllipse(pos, QAngle{-90, 0, 0}, shapeRadius, shapeRadius, 17, color);
			    pos.y += ySpacing;
			    mb.AddEllipse(pos, QAngle{-90, 0, 0}, shapeRadius, -shapeRadius, 17, color);
			    pos.y += ySpacing;
			    mb.AddEllipse(pos, QAngle{-90, 0, 0}, -shapeRadius, shapeRadius, 17, color);
			    pos.y += ySpacing;
			    mb.AddEllipse(pos, QAngle{-90, 0, 0}, -shapeRadius, -shapeRadius, 17, color);

			    pos.y += ySpacing;

			    const Vector extents{10, 10, 10};
			    mb.AddBox(pos, -extents, extents, vec3_angle, color);
			    pos.y += ySpacing;
			    mb.AddBox(pos, extents, -extents, vec3_angle, color);

			    pos.y += ySpacing;

			    mb.AddSphere(pos, shapeRadius, 1, color);
			    pos.y += ySpacing;
			    mb.AddSphere(pos, -shapeRadius, 1, color);

			    pos.y += ySpacing;

			    mb.AddCone(pos, QAngle{-90, 0, 0}, 30, shapeRadius, 5, true, color);
			    pos.y += ySpacing;
			    mb.AddCone(pos, QAngle{-90, 0, 0}, 30, -shapeRadius, 5, true, color);
			    pos.y += ySpacing;
			    mb.AddCone(pos, QAngle{-90, 0, 0}, -30, shapeRadius, 5, true, color);
			    pos.y += ySpacing;
			    mb.AddCone(pos, QAngle{-90, 0, 0}, -30, -shapeRadius, 5, true, color);

			    pos.y += ySpacing;

			    mb.AddCylinder(pos, QAngle{-90, 0, 0}, 30, shapeRadius, 6, true, true, color);
			    pos.y += ySpacing;
			    mb.AddCylinder(pos, QAngle{-90, 0, 0}, 30, -shapeRadius, 6, true, true, color);
			    pos.y += ySpacing;
			    mb.AddCylinder(pos, QAngle{-90, 0, 0}, -30, shapeRadius, 6, true, true, color);
			    pos.y += ySpacing;
			    mb.AddCylinder(pos, QAngle{-90, 0, 0}, -30, -shapeRadius, 6, true, true, color);

			    pos.y += ySpacing;

			    mb.AddArrow3D(pos, pos + Vector{0, 0, 1}, ArrowParams{8, 30.f, 3.f, 0.4f, 2.f}, color);
			    pos.y += ySpacing;
			    mb.AddArrow3D(pos, pos + Vector{0, 0, 1}, ArrowParams{8, 30.f, 3.f, 0.4f, -2.f}, color);
			    pos.y += ySpacing;
			    mb.AddArrow3D(pos, pos + Vector{0, 0, 1}, ArrowParams{8, 30.f, -3.f, 0.4f, 2.f}, color);
			    pos.y += ySpacing;
			    mb.AddArrow3D(pos, pos + Vector{0, 0, 1}, ArrowParams{8, 30.f, -3.f, 0.4f, -2.f}, color);
			    pos.y += ySpacing;
			    mb.AddArrow3D(pos, pos + Vector{0, 0, 1}, ArrowParams{8, -30.f, 3.f, 0.4f, 2.f}, color);
			    pos.y += ySpacing;
			    mb.AddArrow3D(pos, pos + Vector{0, 0, 1}, ArrowParams{8, -30.f, 3.f, 0.4f, -2.f}, color);
			    pos.y += ySpacing;
			    mb.AddArrow3D(pos, pos + Vector{0, 0, 1}, ArrowParams{8, -30.f, -3.f, 0.4f, 2.f}, color);
			    pos.y += ySpacing;
			    mb.AddArrow3D(pos, pos + Vector{0, 0, 1}, ArrowParams{8, -30.f, -3.f, 0.4f, -2.f}, color);

			    pos.y += ySpacing;

			    const float f3 = FastRSqrt(3);
			    VMatrix translateMat{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
			    translateMat.SetTranslation(pos);
			    std::array<VPlane, 4> polyPlanes{
			        VPlane{{f3, f3, f3}, shapeRadius / 2},
			        VPlane{{-f3, -f3, f3}, shapeRadius / 2},
			        VPlane{{f3, -f3, -f3}, shapeRadius / 2},
			        VPlane{{-f3, f3, -f3}, shapeRadius / 2},
			    };
			    for (VPlane& plane : polyPlanes)
				    plane = translateMat * plane;
			    CPolyhedron* poly = GeneratePolyhedronFromPlanes((float*)polyPlanes.data(), 4, 0, true);
			    mb.AddCPolyhedron(poly, color);
			    poly->Release();

			    pos.y += ySpacing;

			    // test main swept box types

			    SweptBoxColor sbColor{{C_OUTLINE(255, 255, 255, 20)},
			                          color,
			                          {C_OUTLINE(100, 100, 100, 20)}};
			    Vector mins{-5, -5, -5};
			    Vector maxs{5, 5, 5};
			    mb.AddSweptBox(pos, pos, mins, maxs, sbColor);
			    pos.y += ySpacing;
			    mb.AddSweptBox(pos, pos + Vector{5, 0, 0}, mins, maxs, sbColor);
			    pos.y += ySpacing;
			    mb.AddSweptBox(pos, pos + Vector{15, 0, 0}, mins, maxs, sbColor);
			    pos.y += ySpacing;
			    mb.AddSweptBox(pos, pos + Vector{5, 5, 0}, mins, maxs, sbColor);
			    pos.y += ySpacing;
			    mb.AddSweptBox(pos, pos + Vector{15, 15, 0}, mins, maxs, sbColor);
			    pos.y += ySpacing;
			    mb.AddSweptBox(pos, pos + Vector{15, 15, 15}, mins, maxs, sbColor);
		    }
	    }));
}

TEST_CASE("AddMbCompactMesh", Vector(1600, -500, 0))
{
	static StaticMesh starMesh, planetMesh, commetMesh;

	constexpr size_t nTwinkleMeshes = 10;
	static std::array<utils::MbCompactMesh, nTwinkleMeshes> twinkleMeshes;
	static std::array<color32, nTwinkleMeshes> twinkleColors;
	static bool twinkleInitialized = false;

	if (!starMesh.Valid())
	{
		utils::MbCompactMesh compactMesh;
		auto& faces = test_data::star_faces;
		for (size_t i = 0; i < ARRAYSIZE(faces); i += 3)
			compactMesh.AddTriangle(faces[i], faces[i + 1], faces[i + 2]);
		// add this one directly, should be visible from the inside
		starMesh = spt_meshBuilder.CreateStaticMesh(
		    [&](MeshBuilderDelegate& mb)
		    { mb.AddMbCompactMesh(compactMesh, true, true, true, true, WD_BOTH); });
	}

	if (!planetMesh.Valid())
	{
		utils::MbCompactMesh compactMesh;
		auto& faces = test_data::planet_faces;
		auto& lines = test_data::planet_lines;
		for (size_t i = 0; i < ARRAYSIZE(faces); i += 3)
			compactMesh.AddTriangle(faces[i + 2], faces[i + 1], faces[i]); // invert .obj winding
		for (size_t i = 0; i < ARRAYSIZE(lines); i += 2)
			compactMesh.AddLine(lines[i], lines[i + 1]);

		// for this mesh, we're gonna add the compact mesh to a dummy dynamic mesh, dump the data, and add it again
		[[maybe_unused]] bool dumpRet = false;
		spt_meshBuilder.CreateDynamicMesh(
		    [&](MeshBuilderDelegate& mb)
		    {
			    mb.AddMbCompactMesh(compactMesh, true, true);
			    compactMesh.Clear();
			    dumpRet = mb.DumpMbCompactMesh(compactMesh);
		    });
		Assert(dumpRet);
		planetMesh = spt_meshBuilder.CreateStaticMesh([&](MeshBuilderDelegate& mb)
		                                              { mb.AddMbCompactMesh(compactMesh, true, true); });
	}

	if (!commetMesh.Valid())
	{
		// for this one we'll use CreateMeshContext and let the builder create the compact mesh for us

		utils::MbCompactMesh compactMesh;
		[[maybe_unused]] bool dumpRet = false;
		auto& faces = test_data::commet_faces;
		auto& lines = test_data::commet_lines;

		spt_meshBuilder.CreateMeshContext(
		    [&](MeshBuilderDelegate& mb)
		    {
			    for (size_t i = 0; i < ARRAYSIZE(faces); i += 3)
			    {
				    color32 color = faces[i + 2].col;
				    color.a *= 0.1f;
				    mb.AddTri(faces[i + 2].pos,
				              faces[i + 1].pos,
				              faces[i].pos,
				              ShapeColor{C_OUTLINE(color)});
			    }
			    for (size_t i = 0; i < ARRAYSIZE(lines); i += 2)
			    {
				    mb.AddLine(lines[i].pos, lines[i + 1].pos, LineColor{lines[i].col});
			    }
			    dumpRet = mb.DumpMbCompactMesh(compactMesh);
		    });

		Assert(dumpRet);
		// now recreate the static mesh using the exported data
		commetMesh = spt_meshBuilder.CreateStaticMesh([&](MeshBuilderDelegate& mb)
		                                              { mb.AddMbCompactMesh(compactMesh, false, true); });
	}

	/*
	* For the twinkle stars, we'll generate the meshes once and use dynamic meshes to set the
	* color. This is also a way to test the serialization/deserialization.
	*/
	for (size_t i = 0; i < nTwinkleMeshes && !twinkleInitialized; i++)
	{
		// pts are the corners of a cube centered at the origin
		// https://en.wikipedia.org/wiki/Tetrahedron#Cartesian_coordinates
		std::array<Vector, 8> pts;
		for (size_t j = 0; j < pts.size(); j++)
			pts[j] = Vector{(j & 1) ? 1.f : -1.f, (j & 2) ? 1.f : -1.f, (j & 4) ? 1.f : -1.f};

		utils::MbCompactMesh& compactMesh = twinkleMeshes[i];
		utils::MbCompactMesh tmpMesh;

		flatbuffers::FlatBufferBuilder fbb{};
		std::vector<uint8_t> writeVec;

		auto randomMat = [this]()
		{
			matrix3x4_t angMat, finalMat;
			SetScaleMatrix(.5f, finalMat);
			Vector offsetFromCenter = vec3_origin;
			while (offsetFromCenter.Length() < 150 || offsetFromCenter.Length() > 300)
				offsetFromCenter = RandomVector(-300, 300);
			AngleMatrix(RandomAngle(-360, 360), testPos + offsetFromCenter, angMat);
			MatrixMultiply(angMat, finalMat, finalMat);
			return finalMat;
		};

		for (size_t j = 0; j < 10; j++)
		{
			tmpMesh.Clear();
			tmpMesh.AddTriangle(pts[4], pts[7], pts[1]);
			tmpMesh.AddTriangle(pts[4], pts[1], pts[2]);
			tmpMesh.AddTriangle(pts[4], pts[2], pts[7]);
			tmpMesh.AddTriangle(pts[1], pts[7], pts[2]);
			tmpMesh.AddLine(pts[5], pts[5] * 2);
			tmpMesh.AddLine(pts[0], pts[0] * 2);
			tmpMesh.AddLine(pts[6], pts[6] * 2);
			tmpMesh.AddLine(pts[3], pts[3] * 2);

			// transform stars in place so that they're elligible for fusing
			matrix3x4_t angMat, finalMat;
			SetScaleMatrix(.5f, finalMat);
			Vector offsetFromCenter = vec3_origin;
			while (offsetFromCenter.Length() < 150 || offsetFromCenter.Length() > 300)
				offsetFromCenter = RandomVector(-300, 300);
			AngleMatrix(RandomAngle(-360, 360), testPos + offsetFromCenter, angMat);
			MatrixMultiply(angMat, finalMat, finalMat);

			if ((i ^ j) & 1)
			{
				// add to the full mesh directly
				[[maybe_unused]] bool ret = compactMesh.AddMbCompactMesh(tmpMesh, randomMat());
				Assert(ret);
			}
			else
			{
				// add by serializing
				flatbuffers::Offset<fb::mesh::MbCompactMesh> fbMesh = tmpMesh.Serialize(fbb, (i ^ j) & 2);
				fbb.FinishSizePrefixed(fbMesh, "MESH");
				auto detached = fbb.Release();
				writeVec.insert(writeVec.end(), detached.begin(), detached.end());
			}
		}

		// deserialize
		ser::StatusTracker stat;
		auto sp = std::span{writeVec};
		while (!sp.empty())
		{
			auto fbMesh = flatbuffers::GetSizePrefixedRoot<fb::mesh::MbCompactMesh>(sp.data());
			compactMesh.Deserialize(*fbMesh, stat, randomMat());
			Assert(stat.Ok());
			size_t len = flatbuffers::GetSizePrefixedBufferLength(sp.data());
			sp = sp.subspan(len);
		}
	}
	for (size_t i = 0; i < nTwinkleMeshes && !twinkleInitialized; i++)
	{
		float t = rand() / (float)RAND_MAX;
		// interpolate between warm (red-orange) and cool (blue-white)
		twinkleColors[i] = {(byte)(255 - t * 105), (byte)(150 + t * 105), (byte)(150 + t * 105), (byte)50};
	}
	twinkleInitialized = true;

	static float remainderTime = 0;
	remainderTime += testFeature.timeSinceLast;

	for (; remainderTime >= 0.01f; remainderTime -= 0.01f)
	{
		for (size_t i = 0; i < nTwinkleMeshes; i++)
		{
			int delta = rand() % 15 - 7;
			twinkleColors[i].a = clamp(twinkleColors[i].a + delta, 20, 90);
		}
	}

	RenderCallback staticCb = [=](const CallbackInfoIn& infoIn, CallbackInfoOut& infoOut)
	{ PositionMatrix(testPos, infoOut.mat); };

	mr.DrawMesh(starMesh, staticCb);
	mr.DrawMesh(planetMesh, staticCb);
	mr.DrawMesh(commetMesh, staticCb);

	for (size_t i = 0; i < nTwinkleMeshes; i++)
		mr.DrawMesh(MB_DYNAMIC(mb.AddMbCompactMesh(twinkleMeshes[i], {twinkleColors[i], twinkleColors[i]});));
}

#endif
