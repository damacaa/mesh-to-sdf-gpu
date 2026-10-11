#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace renderer
{
	class Camera
	{
	public:
		glm::vec3 position;
		glm::vec3 orientation = glm::vec3(0.0f, 0.0f, -1.0f);
		glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
		glm::vec3 right = glm::vec3(1.0f, 0.0f, 0.0f);

		glm::mat4 cameraMatrix = glm::mat4(1.0f);
		glm::mat4 projection = glm::mat4(1.0f);
		glm::mat4 view = glm::mat4(1.0f);

		float fov = 45.0f;
		float nearPlane = 0.1f;
		float farPlane = 300.0f;
		float yaw = -90.0f;
		float pitch = 0.0f;

		Camera(glm::vec3 startPos = glm::vec3(0.0f, 2.0f, 6.0f));

		void updateVectors();
		void updateMatrix(int width, int height);

		void processKeyboard(float moveForward, float moveRight, float moveUp, float deltaTime, float speed = 5.0f);
		void processMouseMovement(float xoffset, float yoffset, float sensitivity = 0.15f);
	};
} // namespace renderer
