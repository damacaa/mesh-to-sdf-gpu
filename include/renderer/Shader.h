#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace renderer
{
	class Shader
	{
	public:
		GLuint id = 0;

		Shader() = default;
		Shader(const std::string& vertexPath, const std::string& fragmentPath);
		~Shader();

		Shader(Shader&& other) noexcept;
		Shader& operator=(Shader&& other) noexcept;

		Shader(const Shader&) = delete;
		Shader& operator=(const Shader&) = delete;

		void use();
		void free();

		bool hasRecompiled() const;
		void setFragmentIncludeCode(int index, const std::string& code, bool shouldRecompile = true);
		void setFragmentIncludeCode(const std::string& includeName, const std::string& code, bool shouldRecompile = true);

		void addDefine(const std::string& name);
		void removeDefine(const std::string& name);
		void toggleDefine(const std::string& name);

		GLint getUniformLocation(const std::string& name) const;

		void setUniform(const std::string& name, float value) const;
		void setUniform(const std::string& name, int value) const;
		void setUniform(const std::string& name, const glm::vec2& value) const;
		void setUniform(const std::string& name, const glm::vec3& value) const;
		void setUniform(const std::string& name, const glm::vec4& value) const;
		void setUniform(const std::string& name, const glm::mat3& value) const;
		void setUniform(const std::string& name, const glm::mat4& value) const;

	private:
		std::string m_vertexPath;
		std::string m_fragmentPath;
		std::filesystem::file_time_type m_lastWriteTime;

		bool m_needsRecompile = false;
		bool m_hasRecompiled = false;

		std::vector<std::string> m_includeNames;
		std::vector<std::string> m_includedFragmentContents;
		std::vector<std::string> m_activeDefines;

		mutable std::unordered_map<std::string, GLint> m_uniformCache;

		void parseIncludes();
		void recompile();
		static std::string readFile(const std::filesystem::path& path);
		static void checkCompileErrors(GLuint shader, const std::string& type);
	};
} // namespace renderer
