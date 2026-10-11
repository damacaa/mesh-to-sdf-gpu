#pragma once

#include <string>
#include <vector>
#include "renderer/Material.h"
#include "renderer/Light.h"

namespace renderer
{
	struct SceneDefinition
	{
		std::string name;
		std::string glslShapesCode;
		glm::vec3 cameraPosition;
		float cameraYaw;
		float cameraPitch;
		std::vector<Material3D> materials;
		std::vector<Light3D> lights;
		bool renderMonkeyMesh = false;
		glm::mat4 monkeyTransform = glm::mat4(1.0f);
		int monkeyMaterialIndex = 0;
	};

	inline std::vector<SceneDefinition> getPredefinedScenes()
	{
		std::vector<SceneDefinition> scenes;

		// -------------------------------------------------------------
		// Scene 1: Cornell Box
		// -------------------------------------------------------------
		{
			SceneDefinition cornell;
			cornell.name = "Cornell Box";
			cornell.cameraPosition = glm::vec3(0.0f, 2.0f, 4.2f);
			cornell.cameraYaw = -90.0f;
			cornell.cameraPitch = 0.0f;
			cornell.renderMonkeyMesh = false;

			// GLSL injected code for Cornell Box shapes
			cornell.glslShapesCode = R"(
	// Floor
	{
		float d = fPlane(p, vec3(0.0, 1.0, 0.0), 0.0);
		if (d < minDist) { minDist = d; finalMaterialId = 0; }
	}
	// Ceiling
	{
		float d = fPlane(p, vec3(0.0, -1.0, 0.0), 4.0);
		if (d < minDist) { minDist = d; finalMaterialId = 0; }
	}
	// Back wall
	{
		float d = fPlane(p, vec3(0.0, 0.0, 1.0), 2.0);
		if (d < minDist) { minDist = d; finalMaterialId = 0; }
	}
	// Left wall (Red)
	{
		float d = fPlane(p, vec3(1.0, 0.0, 0.0), 2.0);
		if (d < minDist) { minDist = d; finalMaterialId = 1; }
	}
	// Right wall (Green)
	{
		float d = fPlane(p, vec3(-1.0, 0.0, 0.0), 2.0);
		if (d < minDist) { minDist = d; finalMaterialId = 2; }
	}
	// Ceiling light fixture
	{
		float d = fBox(p - vec3(0.0, 3.96, 0.0), vec3(0.5, 0.04, 0.5));
		if (d < minDist) { minDist = d; finalMaterialId = 3; }
	}
	// Tall box (rotated)
	{
		vec3 pt = p - vec3(-0.65, 1.2, -0.6);
		pR(pt.xz, 0.35);
		float d = fBox(pt, vec3(0.55, 1.2, 0.55));
		if (d < minDist) { minDist = d; finalMaterialId = 0; }
	}
	// Short box (rotated)
	{
		vec3 ps = p - vec3(0.65, 0.6, 0.3);
		pR(ps.xz, -0.32);
		float d = fBox(ps, vec3(0.55, 0.6, 0.55));
		if (d < minDist) { minDist = d; finalMaterialId = 0; }
	}
)";

			cornell.materials.resize(16);
			// Mat 0: White diffuse walls / boxes
			cornell.materials[0].name = "White Wall";
			cornell.materials[0].color = glm::vec4(0.75f, 0.75f, 0.75f, 1.0f);
			cornell.materials[0].roughness = 0.85f;
			cornell.materials[0].metallic = 0.0f;

			// Mat 1: Red wall
			cornell.materials[1].name = "Red Wall";
			cornell.materials[1].color = glm::vec4(0.85f, 0.12f, 0.12f, 1.0f);
			cornell.materials[1].roughness = 0.85f;
			cornell.materials[1].metallic = 0.0f;

			// Mat 2: Green wall
			cornell.materials[2].name = "Green Wall";
			cornell.materials[2].color = glm::vec4(0.12f, 0.85f, 0.15f, 1.0f);
			cornell.materials[2].roughness = 0.85f;
			cornell.materials[2].metallic = 0.0f;

			// Mat 3: Ceiling light panel
			cornell.materials[3].name = "Ceiling Light";
			cornell.materials[3].color = glm::vec4(1.0f, 0.98f, 0.9f, 1.0f);
			cornell.materials[3].emission = 8.0f;

			// Sun light: Directional light with intensity 0 disables sky illumination (matching weird-engine)
			cornell.lights.push_back({
				.type = 0, // Directional light
				.position = glm::vec3(0.0f, 0.0f, 0.0f),
				.direction = glm::vec3(0.0f, -1.0f, 0.0f),
				.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f) // w = 0.0 turns off sky
			});

			// Ceiling point light
			cornell.lights.push_back({
				.type = 1, // Point light
				.position = glm::vec3(0.0f, 3.85f, 0.0f),
				.direction = glm::vec3(0.0f, -1.0f, 0.0f),
				.color = glm::vec4(1.0f, 0.95f, 0.88f, 2.5f)
			});

			scenes.push_back(cornell);
		}

		// -------------------------------------------------------------
		// Scene 2: Geometric Primitives Showcase
		// -------------------------------------------------------------
		{
			SceneDefinition primitives;
			primitives.name = "Primitives Showcase";
			primitives.cameraPosition = glm::vec3(0.0f, 2.5f, 5.5f);
			primitives.cameraYaw = -90.0f;
			primitives.cameraPitch = -12.0f;
			primitives.renderMonkeyMesh = false;

			primitives.glslShapesCode = R"(
	// Checkered ground plane
	{
		float d = fPlane(p, vec3(0.0, 1.0, 0.0), 0.0);
		if (d < minDist) { minDist = d; finalMaterialId = 0; }
	}
	// Metallic torus in center
	{
		vec3 pt = p - vec3(0.0, 1.5, 0.0);
		pR(pt.xy, 0.4);
		pR(pt.xz, u_time * 0.2);
		float d = fTorus(pt, 0.35, 1.1);
		if (d < minDist) { minDist = d; finalMaterialId = 4; }
	}
	// Golden sphere
	{
		float d = fSphere(p - vec3(-2.2, 1.0, -0.5), 1.0);
		if (d < minDist) { minDist = d; finalMaterialId = 5; }
	}
	// Smooth union of a box and sphere
	{
		vec3 pb = p - vec3(2.2, 1.0, -0.5);
		float dBox = fBox(pb, vec3(0.7, 0.7, 0.7));
		float dSph = fSphere(pb, 0.9);
		float d = fOpUnionSoft(dBox, dSph, 0.3);
		if (d < minDist) { minDist = d; finalMaterialId = 6; }
	}
	// Floating glowing sphere
	{
		float d = fSphere(p - vec3(0.0, 1.5, 0.0), 0.45);
		if (d < minDist) { minDist = d; finalMaterialId = 7; }
	}
	// Columns in background
	{
		vec3 pc1 = p - vec3(-3.5, 2.5, -3.5);
		float d1 = fCylinder(pc1, 0.4, 2.5);
		vec3 pc2 = p - vec3(3.5, 2.5, -3.5);
		float d2 = fCylinder(pc2, 0.4, 2.5);
		float d = min(d1, d2);
		if (d < minDist) { minDist = d; finalMaterialId = 8; }
	}
)";

			primitives.materials.resize(16);
			// Mat 0: Checkered floor
			primitives.materials[0].name = "Checkered Floor";
			primitives.materials[0].color = glm::vec4(0.9f, 0.9f, 0.9f, 1.0f);
			primitives.materials[0].secondaryColor = glm::vec4(0.15f, 0.15f, 0.18f, 1.0f);
			primitives.materials[0].pattern = MaterialPattern::Checkers;
			primitives.materials[0].patternScale = 1.0f;
			primitives.materials[0].roughness = 0.2f;
			primitives.materials[0].metallic = 0.4f;

			// Mat 4: Chrome Torus
			primitives.materials[4].name = "Chrome Torus";
			primitives.materials[4].color = glm::vec4(0.95f, 0.95f, 0.98f, 1.0f);
			primitives.materials[4].metallic = 0.95f;
			primitives.materials[4].roughness = 0.05f;

			// Mat 5: Gold Sphere
			primitives.materials[5].name = "Gold Sphere";
			primitives.materials[5].color = glm::vec4(1.0f, 0.78f, 0.28f, 1.0f);
			primitives.materials[5].metallic = 0.85f;
			primitives.materials[5].roughness = 0.15f;

			// Mat 6: Blue Matte Blend
			primitives.materials[6].name = "Blue Matte";
			primitives.materials[6].color = glm::vec4(0.18f, 0.45f, 0.92f, 1.0f);
			primitives.materials[6].roughness = 0.35f;

			// Mat 7: Emissive Core Orb
			primitives.materials[7].name = "Glowing Core";
			primitives.materials[7].color = glm::vec4(1.0f, 0.35f, 0.1f, 1.0f);
			primitives.materials[7].emission = 6.0f;

			// Mat 8: Stone columns
			primitives.materials[8].name = "Stone Columns";
			primitives.materials[8].color = glm::vec4(0.65f, 0.65f, 0.68f, 1.0f);
			primitives.materials[8].roughness = 0.6f;

			// Directional sun light + point fill
			primitives.lights.push_back({
				.type = 0,
				.direction = glm::vec3(0.5f, -1.0f, -0.6f),
				.color = glm::vec4(1.0f, 0.95f, 0.85f, 2.0f)
			});
			primitives.lights.push_back({
				.type = 1,
				.position = glm::vec3(0.0f, 1.5f, 0.0f),
				.color = glm::vec4(1.0f, 0.4f, 0.15f, 1.5f)
			});

			scenes.push_back(primitives);
		}

		// -------------------------------------------------------------
		// Scene 3: Hybrid Mesh + SDF Playground
		// -------------------------------------------------------------
		{
			SceneDefinition hybrid;
			hybrid.name = "Hybrid Mesh + SDF";
			hybrid.cameraPosition = glm::vec3(0.0f, 2.0f, 4.0f);
			hybrid.cameraYaw = -90.0f;
			hybrid.cameraPitch = -10.0f;
			hybrid.renderMonkeyMesh = true;

			glm::mat4 m = glm::mat4(1.0f);
			m = glm::translate(m, glm::vec3(0.0f, 1.65f, 0.0f));
			m = glm::scale(m, glm::vec3(0.9f));
			hybrid.monkeyTransform = m;
			hybrid.monkeyMaterialIndex = 9; // Matte monkey

			hybrid.glslShapesCode = R"(
	// Reflective floor
	{
		float d = fPlane(p, vec3(0.0, 1.0, 0.0), 0.0);
		if (d < minDist) { minDist = d; finalMaterialId = 0; }
	}
	// Pedestal beneath monkey
	{
		vec3 pp = p - vec3(0.0, 0.25, 0.0);
		float d = fCylinder(pp, 1.2, 0.25);
		if (d < minDist) { minDist = d; finalMaterialId = 10; }
	}
	// Surrounding glowing orbs that cast direct light and indirect color bleeding
	{
		float d1 = fSphere(p - vec3(-2.0, 1.5, -0.5), 0.4);
		if (d1 < minDist) { minDist = d1; finalMaterialId = 11; }

		float d2 = fSphere(p - vec3(2.0, 1.5, -0.5), 0.4);
		if (d2 < minDist) { minDist = d2; finalMaterialId = 12; }
	}
	// Back wall slab
	{
		float d = fBox(p - vec3(0.0, 2.5, -2.5), vec3(4.0, 2.5, 0.2));
		if (d < minDist) { minDist = d; finalMaterialId = 13; }
	}
)";

			hybrid.materials.resize(16);
			// Mat 0: Checkered floor
			hybrid.materials[0].name = "Tile Floor";
			hybrid.materials[0].color = glm::vec4(0.85f, 0.85f, 0.85f, 1.0f);
			hybrid.materials[0].secondaryColor = glm::vec4(0.2f, 0.2f, 0.25f, 1.0f);
			hybrid.materials[0].pattern = MaterialPattern::Checkers;
			hybrid.materials[0].patternScale = 1.0f;
			hybrid.materials[0].roughness = 0.15f;
			hybrid.materials[0].metallic = 0.5f;

			// Mat 9: Matte Suzanne Monkey
			hybrid.materials[9].name = "Matte Monkey";
			hybrid.materials[9].color = glm::vec4(0.85f, 0.55f, 0.28f, 1.0f);
			hybrid.materials[9].metallic = 0.0f;
			hybrid.materials[9].roughness = 1.0f;

			// Mat 10: Marble Pedestal
			hybrid.materials[10].name = "Marble Pedestal";
			hybrid.materials[10].color = glm::vec4(0.92f, 0.92f, 0.94f, 1.0f);
			hybrid.materials[10].roughness = 0.3f;

			// Mat 11: Emissive Blue Orb
			hybrid.materials[11].name = "Cyan Orb";
			hybrid.materials[11].color = glm::vec4(0.1f, 0.8f, 1.0f, 1.0f);
			hybrid.materials[11].emission = 6.0f;

			// Mat 12: Emissive Pink Orb
			hybrid.materials[12].name = "Pink Orb";
			hybrid.materials[12].color = glm::vec4(1.0f, 0.15f, 0.65f, 1.0f);
			hybrid.materials[12].emission = 6.0f;

			// Mat 13: Dark Back Wall
			hybrid.materials[13].name = "Back Wall";
			hybrid.materials[13].color = glm::vec4(0.35f, 0.35f, 0.4f, 1.0f);
			hybrid.materials[13].roughness = 0.7f;

			// Point light above the monkey
			hybrid.lights.push_back({
				.type = 1,
				.position = glm::vec3(0.0f, 3.5f, 0.5f),
				.color = glm::vec4(1.0f, 0.95f, 0.9f, 2.5f)
			});
			// Colored fill point lights near the orbs
			hybrid.lights.push_back({
				.type = 1,
				.position = glm::vec3(-2.0f, 1.5f, -0.5f),
				.color = glm::vec4(0.1f, 0.8f, 1.0f, 1.2f)
			});
			hybrid.lights.push_back({
				.type = 1,
				.position = glm::vec3(2.0f, 1.5f, -0.5f),
				.color = glm::vec4(1.0f, 0.15f, 0.65f, 1.2f)
			});

			scenes.push_back(hybrid);
		}

		return scenes;
	}
} // namespace renderer
