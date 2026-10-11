#pragma once

#include <glm/glm.hpp>

namespace renderer
{
	struct Light3D
	{
		uint32_t type = 0; // 0 = Directional, 1 = Point, 2 = Cone / Spot
		glm::vec3 position = glm::vec3(0.0f);
		glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f);
		glm::vec4 color = glm::vec4(1.0f); // RGB = color, A = intensity
	};
} // namespace renderer
