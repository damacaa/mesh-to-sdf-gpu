#pragma once

#include <glad/glad.h>

namespace renderer
{
	class FullscreenQuad
	{
	public:
		FullscreenQuad();
		~FullscreenQuad();

		void draw() const;
		void free();

	private:
		GLuint m_vao = 0;
		GLuint m_vbo = 0;
		GLuint m_ebo = 0;
	};
} // namespace renderer
