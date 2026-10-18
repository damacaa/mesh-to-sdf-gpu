#include "renderer/Texture3D.h"

#include <utility>

namespace renderer
{
	Texture3D::Texture3D(int width, int height, int depth, GLenum internalFormat)
	{
		allocate(width, height, depth, internalFormat);
	}

	Texture3D::~Texture3D()
	{
		dispose();
	}

	Texture3D::Texture3D(Texture3D&& other) noexcept
		: id(other.id)
		, m_width(other.m_width)
		, m_height(other.m_height)
		, m_depth(other.m_depth)
		, m_internalFormat(other.m_internalFormat)
	{
		other.id = 0;
		other.m_width = 0;
		other.m_height = 0;
		other.m_depth = 0;
	}

	Texture3D& Texture3D::operator=(Texture3D&& other) noexcept
	{
		if (this != &other)
		{
			dispose();
			id = other.id;
			m_width = other.m_width;
			m_height = other.m_height;
			m_depth = other.m_depth;
			m_internalFormat = other.m_internalFormat;

			other.id = 0;
			other.m_width = 0;
			other.m_height = 0;
			other.m_depth = 0;
		}
		return *this;
	}

	void Texture3D::allocate(int width, int height, int depth, GLenum internalFormat)
	{
		dispose();

		m_width = width;
		m_height = height;
		m_depth = depth;
		m_internalFormat = internalFormat;

		glGenTextures(1, &id);
		glBindTexture(GL_TEXTURE_3D, id);

		glTexImage3D(GL_TEXTURE_3D, 0, internalFormat, width, height, depth, 0, GL_RED, GL_FLOAT, nullptr);

		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

		glBindTexture(GL_TEXTURE_3D, 0);
	}

	void Texture3D::bind(GLuint unit) const
	{
		glActiveTexture(GL_TEXTURE0 + unit);
		glBindTexture(GL_TEXTURE_3D, id);
	}

	void Texture3D::bindImage(GLuint unit, GLenum access) const
	{
		glBindImageTexture(unit, id, 0, GL_TRUE, 0, access, m_internalFormat);
	}

	void Texture3D::dispose()
	{
		if (id != 0)
		{
			glDeleteTextures(1, &id);
			id = 0;
		}
		m_width = 0;
		m_height = 0;
		m_depth = 0;
	}
} // namespace renderer
