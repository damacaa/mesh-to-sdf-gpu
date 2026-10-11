#include "renderer/FullscreenQuad.h"

namespace renderer
{
	FullscreenQuad::FullscreenQuad()
	{
		const float f = 1.0f;
		const float vertices[] = {
			// positions (x, y, z)   // texCoords (u, v)
			-f, -f, 0.0f,            0.0f, 0.0f,
			-f,  f, 0.0f,            0.0f, 1.0f,
			 f,  f, 0.0f,            1.0f, 1.0f,
			 f, -f, 0.0f,            1.0f, 0.0f
		};

		const GLuint indices[] = {
			0, 1, 2,
			0, 2, 3
		};

		glGenVertexArrays(1, &m_vao);
		glGenBuffers(1, &m_vbo);
		glGenBuffers(1, &m_ebo);

		glBindVertexArray(m_vao);

		glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(0));
		glEnableVertexAttribArray(0);

		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
		glEnableVertexAttribArray(1);

		glBindVertexArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	}

	FullscreenQuad::~FullscreenQuad()
	{
		free();
	}

	void FullscreenQuad::draw() const
	{
		GLboolean cull = glIsEnabled(GL_CULL_FACE);
		if (cull)
		{
			glDisable(GL_CULL_FACE);
		}

		glBindVertexArray(m_vao);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
		glBindVertexArray(0);

		if (cull)
		{
			glEnable(GL_CULL_FACE);
		}
	}

	void FullscreenQuad::free()
	{
		if (m_vao != 0)
		{
			glDeleteVertexArrays(1, &m_vao);
			glDeleteBuffers(1, &m_vbo);
			glDeleteBuffers(1, &m_ebo);
			m_vao = 0;
			m_vbo = 0;
			m_ebo = 0;
		}
	}
} // namespace renderer
