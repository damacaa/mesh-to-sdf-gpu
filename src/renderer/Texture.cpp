#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

#include "renderer/Texture.h"
#include <iostream>
#include <vector>

namespace renderer
{
	Texture::Texture(const std::string& imagePath)
	{
		stbi_set_flip_vertically_on_load(true);
		unsigned char* bytes = stbi_load(imagePath.c_str(), &m_width, &m_height, &m_channels, 0);

		if (bytes)
		{
			if (m_channels == 4)
			{
				createTexture(bytes, m_width, m_height, TextureType::ColorAlpha);
			}
			else
			{
				createTexture(bytes, m_width, m_height, TextureType::Color);
			}
			glBindTexture(GL_TEXTURE_2D, id);
			glGenerateMipmap(GL_TEXTURE_2D);
			glBindTexture(GL_TEXTURE_2D, 0);
			stbi_image_free(bytes);
		}
		else
		{
			std::cerr << "Failed to load texture image: " << imagePath << std::endl;
		}
	}

	Texture::Texture(const glm::vec4& color)
		: m_width(1)
		, m_height(1)
		, m_channels(4)
	{
		unsigned char pixel[4] = {
			static_cast<unsigned char>(glm::clamp(color.r, 0.0f, 1.0f) * 255.0f),
			static_cast<unsigned char>(glm::clamp(color.g, 0.0f, 1.0f) * 255.0f),
			static_cast<unsigned char>(glm::clamp(color.b, 0.0f, 1.0f) * 255.0f),
			static_cast<unsigned char>(glm::clamp(color.a, 0.0f, 1.0f) * 255.0f)
		};
		createTexture(pixel, 1, 1, TextureType::ColorAlpha);
	}

	Texture::Texture(int width, int height, TextureType type)
		: m_width(width)
		, m_height(height)
	{
		createTexture(nullptr, width, height, type);
	}

	Texture::Texture(Texture&& other) noexcept
		: id(other.id)
		, m_width(other.m_width)
		, m_height(other.m_height)
		, m_channels(other.m_channels)
	{
		other.id = 0;
		other.m_width = 0;
		other.m_height = 0;
		other.m_channels = 0;
	}

	Texture& Texture::operator=(Texture&& other) noexcept
	{
		if (this != &other)
		{
			dispose();
			id = other.id;
			m_width = other.m_width;
			m_height = other.m_height;
			m_channels = other.m_channels;
			other.id = 0;
			other.m_width = 0;
			other.m_height = 0;
			other.m_channels = 0;
		}
		return *this;
	}

	void Texture::createTexture(const void* data, int width, int height, TextureType type)
	{
		m_width = width;
		m_height = height;

		glGenTextures(1, &id);
		glBindTexture(GL_TEXTURE_2D, id);

		switch (type)
		{
			case TextureType::Color:
			{
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
				m_channels = 3;
				break;
			}
			case TextureType::ColorAlpha:
			{
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
				m_channels = 4;
				break;
			}
			case TextureType::LinearData:
			{
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, data);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
				m_channels = 4;
				break;
			}
			case TextureType::AccumulationData:
			{
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, data);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
				m_channels = 4;
				break;
			}
			case TextureType::Depth:
			{
				glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, data);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
				m_channels = 1;
				break;
			}
			case TextureType::IntData:
			{
				glTexImage2D(GL_TEXTURE_2D, 0, GL_R32I, width, height, 0, GL_RED_INTEGER, GL_INT, data);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
				m_channels = 1;
				break;
			}
		}

		glBindTexture(GL_TEXTURE_2D, 0);
	}

	void Texture::bind(GLuint unit) const
	{
		glActiveTexture(GL_TEXTURE0 + unit);
		glBindTexture(GL_TEXTURE_2D, id);
	}

	void Texture::dispose()
	{
		if (id != 0)
		{
			glDeleteTextures(1, &id);
			id = 0;
		}
	}

	void Texture::saveToDisk(const std::string& filename)
	{
		if (id == 0 || m_width <= 0 || m_height <= 0)
		{
			return;
		}

		glBindTexture(GL_TEXTURE_2D, id);

		std::vector<float> pixels(static_cast<size_t>(m_width) * m_height * 4);
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels.data());

		std::vector<unsigned char> flippedPixels(static_cast<size_t>(m_width) * m_height * 4);
		const size_t pixelSize = 4;

		for (int y = 0; y < m_height; ++y)
		{
			size_t srcRowStart = pixelSize * static_cast<size_t>(m_width) * y;
			size_t destRowStart = pixelSize * static_cast<size_t>(m_width) * (m_height - 1 - y);

			for (int x = 0; x < m_width; ++x)
			{
				size_t srcIdx = srcRowStart + pixelSize * x;
				size_t destIdx = destRowStart + pixelSize * x;

				flippedPixels[destIdx + 0] = static_cast<unsigned char>(glm::clamp(pixels[srcIdx + 0], 0.0f, 1.0f) * 255.0f);
				flippedPixels[destIdx + 1] = static_cast<unsigned char>(glm::clamp(pixels[srcIdx + 1], 0.0f, 1.0f) * 255.0f);
				flippedPixels[destIdx + 2] = static_cast<unsigned char>(glm::clamp(pixels[srcIdx + 2], 0.0f, 1.0f) * 255.0f);
				flippedPixels[destIdx + 3] = 255;
			}
		}

		if (filename.ends_with(".bmp"))
		{
			stbi_write_bmp(filename.c_str(), m_width, m_height, 4, flippedPixels.data());
		}
		else
		{
			stbi_write_png(filename.c_str(), m_width, m_height, 4, flippedPixels.data(), m_width * 4);
		}
	}
} // namespace renderer
