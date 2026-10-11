#include "renderer/Shader.h"

#include <fstream>
#include <iostream>
#include <sstream>

namespace renderer
{
	std::string Shader::readFile(const std::filesystem::path& path)
	{
		std::ifstream file(path);
		if (!file.is_open())
		{
			std::cerr << "Failed to open file: " << path << std::endl;
			return "";
		}
		std::stringstream buffer;
		buffer << file.rdbuf();
		return buffer.str();
	}

	Shader::Shader(const std::string& vertexPath, const std::string& fragmentPath)
		: m_vertexPath(vertexPath)
		, m_fragmentPath(fragmentPath)
	{
		if (std::filesystem::exists(fragmentPath))
		{
			m_lastWriteTime = std::filesystem::last_write_time(fragmentPath);
		}
		parseIncludes();
		recompile();
	}

	Shader::~Shader()
	{
		free();
	}

	Shader::Shader(Shader&& other) noexcept
		: id(other.id)
		, m_vertexPath(std::move(other.m_vertexPath))
		, m_fragmentPath(std::move(other.m_fragmentPath))
		, m_lastWriteTime(other.m_lastWriteTime)
		, m_needsRecompile(other.m_needsRecompile)
		, m_hasRecompiled(other.m_hasRecompiled)
		, m_includeNames(std::move(other.m_includeNames))
		, m_includedFragmentContents(std::move(other.m_includedFragmentContents))
		, m_activeDefines(std::move(other.m_activeDefines))
		, m_uniformCache(std::move(other.m_uniformCache))
	{
		other.id = 0;
	}

	Shader& Shader::operator=(Shader&& other) noexcept
	{
		if (this != &other)
		{
			free();
			id = other.id;
			m_vertexPath = std::move(other.m_vertexPath);
			m_fragmentPath = std::move(other.m_fragmentPath);
			m_lastWriteTime = other.m_lastWriteTime;
			m_needsRecompile = other.m_needsRecompile;
			m_hasRecompiled = other.m_hasRecompiled;
			m_includeNames = std::move(other.m_includeNames);
			m_includedFragmentContents = std::move(other.m_includedFragmentContents);
			m_activeDefines = std::move(other.m_activeDefines);
			m_uniformCache = std::move(other.m_uniformCache);
			other.id = 0;
		}
		return *this;
	}

	void Shader::use()
	{
		m_hasRecompiled = false;

		if (std::filesystem::exists(m_fragmentPath))
		{
			auto currentTime = std::filesystem::last_write_time(m_fragmentPath);
			if (currentTime != m_lastWriteTime)
			{
				m_lastWriteTime = currentTime;
				m_needsRecompile = true;
			}
		}

		if (m_needsRecompile)
		{
			m_needsRecompile = false;
			recompile();
			m_hasRecompiled = true;
		}

		if (id != 0)
		{
			glUseProgram(id);
		}
	}

	void Shader::free()
	{
		if (id != 0)
		{
			glDeleteProgram(id);
			id = 0;
		}
	}

	bool Shader::hasRecompiled() const
	{
		return m_hasRecompiled;
	}

	void Shader::parseIncludes()
	{
		m_includeNames.clear();
		m_includedFragmentContents.clear();

		std::string source = readFile(m_fragmentPath);
		if (source.empty())
		{
			return;
		}

		std::filesystem::path baseDir = std::filesystem::path(m_fragmentPath).parent_path();
		size_t pos = 0;

		while (true)
		{
			size_t incStart = source.find("#include", pos);
			if (incStart == std::string::npos)
			{
				break;
			}

			size_t firstQuote = source.find('"', incStart);
			if (firstQuote == std::string::npos)
			{
				break;
			}

			size_t secondQuote = source.find('"', firstQuote + 1);
			if (secondQuote == std::string::npos)
			{
				break;
			}

			std::string incName = source.substr(firstQuote + 1, secondQuote - firstQuote - 1);
			m_includeNames.push_back(incName);

			std::filesystem::path resolvedPath = baseDir / incName;
			if (!std::filesystem::exists(resolvedPath))
			{
				resolvedPath = baseDir.parent_path() / incName;
			}
			if (!std::filesystem::exists(resolvedPath))
			{
				resolvedPath = baseDir.parent_path() / "common" / incName;
			}

			if (std::filesystem::exists(resolvedPath) && std::filesystem::is_regular_file(resolvedPath))
			{
				m_includedFragmentContents.push_back(readFile(resolvedPath));
			}
			else
			{
				// Procedural include slot
				m_includedFragmentContents.push_back("");
			}

			pos = secondQuote + 1;
		}
	}

	void Shader::setFragmentIncludeCode(int index, const std::string& code, bool shouldRecompile)
	{
		if (index >= 0 && index < static_cast<int>(m_includedFragmentContents.size()))
		{
			m_includedFragmentContents[index] = code;
			if (shouldRecompile)
			{
				recompile();
				m_hasRecompiled = true;
			}
		}
	}

	void Shader::setFragmentIncludeCode(const std::string& includeName, const std::string& code, bool shouldRecompile)
	{
		for (size_t i = 0; i < m_includeNames.size(); ++i)
		{
			if (m_includeNames[i] == includeName)
			{
				setFragmentIncludeCode(static_cast<int>(i), code, shouldRecompile);
				return;
			}
		}
	}

	void Shader::addDefine(const std::string& name)
	{
		for (const auto& d : m_activeDefines)
		{
			if (d == name)
				return;
		}
		m_activeDefines.push_back(name);
		m_needsRecompile = true;
	}

	void Shader::removeDefine(const std::string& name)
	{
		for (auto it = m_activeDefines.begin(); it != m_activeDefines.end(); ++it)
		{
			if (*it == name)
			{
				m_activeDefines.erase(it);
				m_needsRecompile = true;
				return;
			}
		}
	}

	void Shader::toggleDefine(const std::string& name)
	{
		for (auto it = m_activeDefines.begin(); it != m_activeDefines.end(); ++it)
		{
			if (*it == name)
			{
				m_activeDefines.erase(it);
				m_needsRecompile = true;
				return;
			}
		}
		m_activeDefines.push_back(name);
		m_needsRecompile = true;
	}

	void Shader::recompile()
	{
		std::string vertexSource = readFile(m_vertexPath);
		std::string fragmentRaw = readFile(m_fragmentPath);

		if (vertexSource.empty() || fragmentRaw.empty())
		{
			return;
		}

		std::string fragmentSource;
		fragmentSource.reserve(fragmentRaw.size() + 4096);

		size_t firstNewline = fragmentRaw.find('\n');
		size_t lastPos = 0;

		if (firstNewline != std::string::npos)
		{
			fragmentSource.append(fragmentRaw.substr(0, firstNewline + 1));
			lastPos = firstNewline + 1;
		}

		for (const auto& define : m_activeDefines)
		{
			fragmentSource.append("#define " + define + "\n");
		}

		size_t includeIndex = 0;
		while (includeIndex < m_includedFragmentContents.size())
		{
			size_t incStart = fragmentRaw.find("#include", lastPos);
			if (incStart == std::string::npos)
			{
				break;
			}

			size_t quoteStart = fragmentRaw.find('"', incStart);
			if (quoteStart == std::string::npos)
			{
				break;
			}

			size_t quoteEnd = fragmentRaw.find('"', quoteStart + 1);
			if (quoteEnd == std::string::npos)
			{
				break;
			}

			fragmentSource.append(fragmentRaw.substr(lastPos, incStart - lastPos));
			fragmentSource.append(m_includedFragmentContents[includeIndex]);
			includeIndex++;
			lastPos = quoteEnd + 1;
		}

		fragmentSource.append(fragmentRaw.substr(lastPos));

		GLuint vs = glCreateShader(GL_VERTEX_SHADER);
		const char* vsSrc = vertexSource.c_str();
		glShaderSource(vs, 1, &vsSrc, nullptr);
		glCompileShader(vs);
		checkCompileErrors(vs, "VERTEX");

		GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
		const char* fsSrc = fragmentSource.c_str();
		glShaderSource(fs, 1, &fsSrc, nullptr);
		glCompileShader(fs);
		checkCompileErrors(fs, "FRAGMENT");

		GLint vsSuccess = 0;
		GLint fsSuccess = 0;
		glGetShaderiv(vs, GL_COMPILE_STATUS, &vsSuccess);
		glGetShaderiv(fs, GL_COMPILE_STATUS, &fsSuccess);

		if (!vsSuccess || !fsSuccess)
		{
			glDeleteShader(vs);
			glDeleteShader(fs);
			return;
		}

		GLuint newProgram = glCreateProgram();
		glAttachShader(newProgram, vs);
		glAttachShader(newProgram, fs);
		glLinkProgram(newProgram);
		checkCompileErrors(newProgram, "PROGRAM");

		glDeleteShader(vs);
		glDeleteShader(fs);

		GLint linkSuccess = 0;
		glGetProgramiv(newProgram, GL_LINK_STATUS, &linkSuccess);
		if (linkSuccess)
		{
			if (id != 0)
			{
				glDeleteProgram(id);
			}
			id = newProgram;
			m_uniformCache.clear();
		}
		else
		{
			glDeleteProgram(newProgram);
		}
	}

	void Shader::checkCompileErrors(GLuint shader, const std::string& type)
	{
		GLint success;
		GLchar infoLog[1024];
		if (type != "PROGRAM")
		{
			glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
			if (!success)
			{
				glGetShaderInfoLog(shader, sizeof(infoLog), nullptr, infoLog);
				std::cerr << "ERROR: Shader compilation of type " << type << "\n" << infoLog << "\n";
			}
		}
		else
		{
			glGetProgramiv(shader, GL_LINK_STATUS, &success);
			if (!success)
			{
				glGetProgramInfoLog(shader, sizeof(infoLog), nullptr, infoLog);
				std::cerr << "ERROR: Program linking of type " << type << "\n" << infoLog << "\n";
			}
		}
	}

	GLint Shader::getUniformLocation(const std::string& name) const
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

	void Shader::setUniform(const std::string& name, float value) const
	{
		glUniform1f(getUniformLocation(name), value);
	}

	void Shader::setUniform(const std::string& name, int value) const
	{
		glUniform1i(getUniformLocation(name), value);
	}

	void Shader::setUniform(const std::string& name, const glm::vec2& value) const
	{
		glUniform2fv(getUniformLocation(name), 1, glm::value_ptr(value));
	}

	void Shader::setUniform(const std::string& name, const glm::vec3& value) const
	{
		glUniform3fv(getUniformLocation(name), 1, glm::value_ptr(value));
	}

	void Shader::setUniform(const std::string& name, const glm::vec4& value) const
	{
		glUniform4fv(getUniformLocation(name), 1, glm::value_ptr(value));
	}

	void Shader::setUniform(const std::string& name, const glm::mat3& value) const
	{
		glUniformMatrix3fv(getUniformLocation(name), 1, GL_FALSE, glm::value_ptr(value));
	}

	void Shader::setUniform(const std::string& name, const glm::mat4& value) const
	{
		glUniformMatrix4fv(getUniformLocation(name), 1, GL_FALSE, glm::value_ptr(value));
	}
} // namespace renderer
