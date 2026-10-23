#include "renderer/Texture3D.h"

#include <fstream>
#include <iostream>
#include <utility>
#include <vector>

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
		allocateWithData(width, height, depth, internalFormat, GL_RED, GL_FLOAT, nullptr, GL_LINEAR, GL_CLAMP_TO_EDGE);
	}

	void Texture3D::allocateWithData(int width, int height, int depth,
	                                GLenum internalFormat, GLenum format, GLenum type,
	                                const void* data,
	                                GLenum filter, GLenum wrap)
	{
		dispose();

		m_width = width;
		m_height = height;
		m_depth = depth;
		m_internalFormat = internalFormat;

		glGenTextures(1, &id);
		glBindTexture(GL_TEXTURE_3D, id);

		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage3D(GL_TEXTURE_3D, 0, internalFormat, width, height, depth, 0, format, type, data);

		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, filter);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, filter);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, wrap);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, wrap);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, wrap);

		glBindTexture(GL_TEXTURE_3D, 0);
	}

	bool Texture3D::loadFromFile(const std::string& filePath, int width, int height, int depth,
	                             GLenum internalFormat, GLenum format, GLenum type,
	                             GLenum filter, GLenum wrap)
	{
		std::ifstream file(filePath, std::ios::binary);
		if (!file.is_open())
		{
			std::cerr << "Texture3D::loadFromFile: Failed to open file " << filePath << std::endl;
			return false;
		}

		size_t channels = 4;
		if (format == GL_RED)
		{
			channels = 1;
		}
		else if (format == GL_RG)
		{
			channels = 2;
		}
		else if (format == GL_RGB)
		{
			channels = 3;
		}
		else if (format == GL_RGBA)
		{
			channels = 4;
		}

		size_t bytesPerChannel = 1;
		if (type == GL_FLOAT)
		{
			bytesPerChannel = 4;
		}
		else if (type == GL_HALF_FLOAT)
		{
			bytesPerChannel = 2;
		}

		size_t expectedSize = static_cast<size_t>(width) * height * depth * channels * bytesPerChannel;
		std::vector<unsigned char> buffer(expectedSize);
		file.read(reinterpret_cast<char*>(buffer.data()), expectedSize);
		std::streamsize bytesRead = file.gcount();
		file.close();

		if (bytesRead < static_cast<std::streamsize>(expectedSize))
		{
			std::cerr << "Texture3D::loadFromFile: Expected " << expectedSize << " bytes, read " << bytesRead << " from " << filePath << std::endl;
			return false;
		}

		allocateWithData(width, height, depth, internalFormat, format, type, buffer.data(), filter, wrap);
		return true;
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
