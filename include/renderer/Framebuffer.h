#pragma once

#include <glad/glad.h>
#include "renderer/Texture.h"
#include <vector>

namespace renderer
{
	class Framebuffer
	{
	public:
		GLuint id = 0;

		Framebuffer();
		~Framebuffer();

		Framebuffer(Framebuffer&& other) noexcept;
		Framebuffer& operator=(Framebuffer&& other) noexcept;

		Framebuffer(const Framebuffer&) = delete;
		Framebuffer& operator=(const Framebuffer&) = delete;

		void bind() const;
		static void unbind();
		void free();

		void attachColorTexture(const Texture& texture, int slot = 0);
		void attachDepthTexture(const Texture& texture);

		int getWidth() const { return m_width; }
		int getHeight() const { return m_height; }

	private:
		int m_width = 0;
		int m_height = 0;
		std::vector<GLuint> m_colorAttachments;
	};
} // namespace renderer
