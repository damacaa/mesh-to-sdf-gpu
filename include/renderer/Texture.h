#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>

namespace renderer
{
	class Texture
	{
	public:
		enum class TextureType
		{
			Color,
			ColorAlpha,
			LinearData,
			AccumulationData,
			Depth,
			IntData
		};

		GLuint id = 0;

		Texture() = default;
		Texture(const std::string& imagePath);
		Texture(const glm::vec4& color);
		Texture(int width, int height, TextureType type);

		Texture(Texture&& other) noexcept;
		Texture& operator=(Texture&& other) noexcept;

		Texture(const Texture&) = delete;
		Texture& operator=(const Texture&) = delete;

		void bind(GLuint unit = 0) const;
		void dispose();
		void saveToDisk(const std::string& filename);

		int getWidth() const { return m_width; }
		int getHeight() const { return m_height; }

	private:
		int m_width = 0;
		int m_height = 0;
		int m_channels = 0;

		void createTexture(const void* data, int width, int height, TextureType type);
	};
} // namespace renderer
