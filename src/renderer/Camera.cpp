#include "renderer/Camera.h"

namespace renderer
{
	Camera::Camera(glm::vec3 startPos)
		: position(startPos)
	{
		updateVectors();
	}

	void Camera::updateVectors()
	{
		glm::vec3 front;
		front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
		front.y = sin(glm::radians(pitch));
		front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
		orientation = glm::normalize(front);
		right = glm::normalize(glm::cross(orientation, glm::vec3(0.0f, 1.0f, 0.0f)));
		up = glm::normalize(glm::cross(right, orientation));
	}

	void Camera::updateMatrix(int width, int height)
	{
		prevCameraMatrix = cameraMatrix;
		float aspect = static_cast<float>(width) / static_cast<float>(height > 0 ? height : 1);
		view = glm::lookAt(position, position + orientation, up);
		projection = glm::perspective(glm::radians(fov), aspect, nearPlane, farPlane);
		cameraMatrix = projection * view;
		if (prevCameraMatrix == glm::mat4(1.0f))
		{
			prevCameraMatrix = cameraMatrix;
		}
	}

	void Camera::processKeyboard(float moveForward, float moveRight, float moveUp, float deltaTime, float speed)
	{
		float velocity = speed * deltaTime;
		position += orientation * (moveForward * velocity);
		position += right * (moveRight * velocity);
		position += up * (moveUp * velocity);
	}

	void Camera::processMouseMovement(float xoffset, float yoffset, float sensitivity)
	{
		yaw += xoffset * sensitivity;
		pitch -= yoffset * sensitivity;

		if (pitch > 89.0f)
			pitch = 89.0f;
		if (pitch < -89.0f)
			pitch = -89.0f;

		updateVectors();
	}
} // namespace renderer
