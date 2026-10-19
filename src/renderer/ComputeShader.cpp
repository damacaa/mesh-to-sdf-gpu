#include "renderer/ComputeShader.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <glm/gtc/type_ptr.hpp>

namespace renderer
{
	std::string ComputeShader::readFile(const std::filesystem::path& path)
	{
		std::ifstream file(path);
		if (!file.is_open())
		{
			std::cerr << "Failed to open compute shader file: " << path << std::endl;
			return "";
		}
		std::stringstream buffer;
		buffer << file.rdbuf();
		return buffer.str();
	}

	ComputeShader::ComputeShader(const std::string& computePath)
		: m_computePath(computePath)
	{
		std::string rawSource = readFile(computePath);
		if (!rawSource.empty())
		{
			std::filesystem::path baseDir = std::filesystem::path(computePath).parent_path();
			std::string processedSource = resolveIncludes(rawSource, baseDir);
			compile(processedSource);
		}
	}

	ComputeShader::~ComputeShader()
	{
		free();
	}

	ComputeShader::ComputeShader(ComputeShader&& other) noexcept
		: id(other.id)
		, m_computePath(std::move(other.m_computePath))
		, m_uniformCache(std::move(other.m_uniformCache))
	{
		other.id = 0;
	}

	ComputeShader& ComputeShader::operator=(ComputeShader&& other) noexcept
	{
		if (this != &other)
		{
			free();
			id = other.id;
			m_computePath = std::move(other.m_computePath);
			m_uniformCache = std::move(other.m_uniformCache);
			other.id = 0;
		}
		return *this;
	}

	void ComputeShader::use() const
	{
		if (id != 0)
		{
			glUseProgram(id);
		}
	}

	void ComputeShader::free()
	{
		if (id != 0)
		{
			glDeleteProgram(id);
			id = 0;
		}
		m_uniformCache.clear();
	}

	void ComputeShader::dispatch(GLuint groupsX, GLuint groupsY, GLuint groupsZ) const
	{
		if (id != 0)
		{
			glDispatchCompute(groupsX, groupsY, groupsZ);
		}
	}

	void ComputeShader::memoryBarrier(GLbitfield barriers) const
	{
		glMemoryBarrier(barriers);
	}

	std::string ComputeShader::resolveIncludes(const std::string& source, const std::filesystem::path& baseDir)
	{
		std::string result;
		std::istringstream stream(source);
		std::string line;

		while (std::getline(stream, line))
		{
			size_t incPos = line.find("#include");
			if (incPos != std::string::npos)
			{
				size_t firstQuote = line.find('"', incPos);
				size_t secondQuote = line.find('"', firstQuote + 1);
				if (firstQuote != std::string::npos && secondQuote != std::string::npos)
				{
					std::string incName = line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
					std::filesystem::path incPath = baseDir / incName;
					if (!std::filesystem::exists(incPath))
					{
						incPath = baseDir.parent_path() / incName;
					}
					if (!std::filesystem::exists(incPath))
					{
						incPath = baseDir.parent_path() / "common" / incName;
					}

					if (std::filesystem::exists(incPath))
					{
						std::string incContent = readFile(incPath);
						result += resolveIncludes(incContent, incPath.parent_path()) + "\n";
						continue;
					}
					else
					{
						std::cerr << "ComputeShader include not found: " << incName << std::endl;
					}
				}
			}
			result += line + "\n";
		}
		return result;
	}

	void ComputeShader::compile(const std::string& source)
	{
		free();

		GLuint computeShader = glCreateShader(GL_COMPUTE_SHADER);
		const char* src = source.c_str();
		glShaderSource(computeShader, 1, &src, nullptr);
		glCompileShader(computeShader);
		checkCompileErrors(computeShader, "COMPUTE");

		id = glCreateProgram();
		glAttachShader(id, computeShader);
		glLinkProgram(id);
		checkCompileErrors(id, "PROGRAM");

		glDeleteShader(computeShader);
	}

	void ComputeShader::checkCompileErrors(GLuint shader, const std::string& type)
	{
		GLint success = 0;
		GLchar infoLog[1024];

		if (type != "PROGRAM")
		{
			glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
			if (!success)
			{
				glGetShaderInfoLog(shader, sizeof(infoLog), nullptr, infoLog);
				std::cerr << "Compute Shader Compilation Error (" << type << "):\n" << infoLog << std::endl;
			}
		}
		else
		{
			glGetProgramiv(shader, GL_LINK_STATUS, &success);
			if (!success)
			{
				glGetProgramInfoLog(shader, sizeof(infoLog), nullptr, infoLog);
				std::cerr << "Compute Program Linking Error:\n" << infoLog << std::endl;
			}
		}
	}

	GLint ComputeShader::getUniformLocation(const std::string& name) const
	{
		auto it = m_uniformCache.find(name);
		if (it != m_uniformCache.end())
		{
			return it->second;
		}
		GLint location = glGetUniformLocation(id, name.c_str());
		m_uniformCache[name] = location;
		return location;
	}

	void ComputeShader::setUniform(const std::string& name, float value) const
	{
		GLint loc = getUniformLocation(name);
		if (loc != -1)
		{
			glUniform1f(loc, value);
		}
	}

	void ComputeShader::setUniform(const std::string& name, int value) const
	{
		GLint loc = getUniformLocation(name);
		if (loc != -1)
		{
			glUniform1i(loc, value);
		}
	}

	void ComputeShader::setUniform(const std::string& name, const glm::ivec3& value) const
	{
		GLint loc = getUniformLocation(name);
		if (loc != -1)
		{
			glUniform3i(loc, value.x, value.y, value.z);
		}
	}

	void ComputeShader::setUniform(const std::string& name, const glm::vec2& value) const
	{
		GLint loc = getUniformLocation(name);
		if (loc != -1)
		{
			glUniform2fv(loc, 1, glm::value_ptr(value));
		}
	}

	void ComputeShader::setUniform(const std::string& name, const glm::vec3& value) const
	{
		GLint loc = getUniformLocation(name);
		if (loc != -1)
		{
			glUniform3fv(loc, 1, glm::value_ptr(value));
		}
	}

	void ComputeShader::setUniform(const std::string& name, const glm::vec4& value) const
	{
		GLint loc = getUniformLocation(name);
		if (loc != -1)
		{
			glUniform4fv(loc, 1, glm::value_ptr(value));
		}
	}

	void ComputeShader::setUniform(const std::string& name, const glm::mat4& value) const
	{
		GLint loc = getUniformLocation(name);
		if (loc != -1)
		{
			glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(value));
		}
	}
} // namespace renderer
