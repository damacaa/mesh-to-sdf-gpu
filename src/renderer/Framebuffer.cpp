#include "renderer/Framebuffer.h"

#include <iostream>

namespace renderer
{
	Framebuffer::Framebuffer()
	{
		glGenFramebuffers(1, &id);
	}

	Framebuffer::~Framebuffer()
	{
		free();
	}

	Framebuffer::Framebuffer(Framebuffer&& other) noexcept
		: id(other.id)
		, m_width(other.m_width)
		, m_height(other.m_height)
		, m_colorAttachments(std::move(other.m_colorAttachments))
	{
		other.id = 0;
		other.m_width = 0;
		other.m_height = 0;
	}

	Framebuffer& Framebuffer::operator=(Framebuffer&& other) noexcept
	{
		if (this != &other)
		{
			free();
			id = other.id;
			m_width = other.m_width;
			m_height = other.m_height;
			m_colorAttachments = std::move(other.m_colorAttachments);
			other.id = 0;
			other.m_width = 0;
			other.m_height = 0;
		}
		return *this;
	}

	void Framebuffer::bind() const
	{
		glBindFramebuffer(GL_FRAMEBUFFER, id);
		glViewport(0, 0, m_width, m_height);
	}

	void Framebuffer::unbind()
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	void Framebuffer::free()
	{
		if (id != 0)
		{
			glDeleteFramebuffers(1, &id);
			id = 0;
		}
	}

	void Framebuffer::attachColorTexture(const Texture& texture, int slot)
	{
		m_width = texture.getWidth();
		m_height = texture.getHeight();

		glBindFramebuffer(GL_FRAMEBUFFER, id);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + slot, GL_TEXTURE_2D, texture.id, 0);

		if (slot >= static_cast<int>(m_colorAttachments.size()))
		{
			m_colorAttachments.resize(slot + 1);
		}
		m_colorAttachments[slot] = GL_COLOR_ATTACHMENT0 + slot;

		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		{
			std::cerr << "Framebuffer is incomplete after color attachment!" << std::endl;
		}

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	void Framebuffer::attachDepthTexture(const Texture& texture)
	{
		m_width = texture.getWidth();
		m_height = texture.getHeight();

		glBindFramebuffer(GL_FRAMEBUFFER, id);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, texture.id, 0);

		if (m_colorAttachments.empty())
		{
			glDrawBuffer(GL_NONE);
			glReadBuffer(GL_NONE);
		}

		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		{
			std::cerr << "Framebuffer is incomplete after depth attachment!" << std::endl;
		}

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}
} // namespace renderer
