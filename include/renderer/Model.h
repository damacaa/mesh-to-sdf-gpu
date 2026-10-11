#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include "renderer/Shader.h"
#include "renderer/Texture.h"

namespace renderer
{
	struct Vertex
	{
		glm::vec3 position;
		glm::vec3 normal;
		glm::vec3 color;
		glm::vec2 texUV;
	};

	class Model
	{
	public:
		Model() = default;
		explicit Model(const std::string& gltfPath);
		~Model();

		bool loadGLTF(const std::string& gltfPath);
		void draw(const Shader& shader, const glm::mat4& modelMatrix, int materialIndex = 0) const;
		void free();

		bool isValid() const { return m_indexCount > 0; }

	private:
		GLuint m_vao = 0;
		GLuint m_vbo = 0;
		GLuint m_ebo = 0;
		GLsizei m_indexCount = 0;

		std::vector<Texture> m_textures;
	};
} // namespace renderer
