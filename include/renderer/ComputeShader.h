#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace renderer
{
	class ComputeShader
	{
	public:
		GLuint id = 0;

		ComputeShader() = default;
		explicit ComputeShader(const std::string& computePath);
		~ComputeShader();

		ComputeShader(ComputeShader&& other) noexcept;
		ComputeShader& operator=(ComputeShader&& other) noexcept;

		ComputeShader(const ComputeShader&) = delete;
		ComputeShader& operator=(const ComputeShader&) = delete;

		void use() const;
		void free();

		void dispatch(GLuint groupsX, GLuint groupsY, GLuint groupsZ) const;
		void memoryBarrier(GLbitfield barriers = GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT) const;

		GLint getUniformLocation(const std::string& name) const;

		void setUniform(const std::string& name, float value) const;
		void setUniform(const std::string& name, int value) const;
		void setUniform(const std::string& name, const glm::ivec3& value) const;
		void setUniform(const std::string& name, const glm::vec2& value) const;
		void setUniform(const std::string& name, const glm::vec3& value) const;
		void setUniform(const std::string& name, const glm::vec4& value) const;
		void setUniform(const std::string& name, const glm::mat4& value) const;

	private:
		std::string m_computePath;
		mutable std::unordered_map<std::string, GLint> m_uniformCache;

		void compile(const std::string& source);
		static std::string readFile(const std::filesystem::path& path);
		static std::string resolveIncludes(const std::string& source, const std::filesystem::path& baseDir);
		static void checkCompileErrors(GLuint shader, const std::string& type);
	};
} // namespace renderer
