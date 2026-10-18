#pragma once

#include <glad/glad.h>

namespace renderer
{
	class Texture3D
	{
	public:
		GLuint id = 0;

		Texture3D() = default;
		Texture3D(int width, int height, int depth, GLenum internalFormat = GL_R32F);
		~Texture3D();

		Texture3D(Texture3D&& other) noexcept;
		Texture3D& operator=(Texture3D&& other) noexcept;

		Texture3D(const Texture3D&) = delete;
		Texture3D& operator=(const Texture3D&) = delete;

		void allocate(int width, int height, int depth, GLenum internalFormat = GL_R32F);
		void bind(GLuint unit = 0) const;
		void bindImage(GLuint unit = 0, GLenum access = GL_WRITE_ONLY) const;
		void dispose();

		int getWidth() const { return m_width; }
		int getHeight() const { return m_height; }
		int getDepth() const { return m_depth; }
		GLenum getInternalFormat() const { return m_internalFormat; }

	private:
		int m_width = 0;
		int m_height = 0;
		int m_depth = 0;
		GLenum m_internalFormat = GL_R32F;
	};
} // namespace renderer
