#pragma once

#include <glm/glm.hpp>
#include <string>

namespace renderer
{
	enum class MaterialPattern : uint32_t
	{
		None = 0,
		Checkers = 1,
		PerlinNoise = 2,
		Waves = 3
	};

	struct Material3D
	{
		uint16_t id = 0;
		std::string name = "";
		glm::vec4 color = glm::vec4(1.0f);
		glm::vec4 secondaryColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		float metallic = 0.0f;
		float roughness = 1.0f;
		MaterialPattern pattern = MaterialPattern::None;
		float patternScale = 1.0f;
		float emission = 0.0f;
	};
} // namespace renderer
