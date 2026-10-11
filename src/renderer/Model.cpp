#include "renderer/Model.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <json/json.h>

using json = nlohmann::json;

namespace renderer
{
	Model::Model(const std::string& gltfPath)
	{
		loadGLTF(gltfPath);
	}

	Model::~Model()
	{
		free();
	}

	void Model::free()
	{
		if (m_vao != 0)
		{
			glDeleteVertexArrays(1, &m_vao);
			glDeleteBuffers(1, &m_vbo);
			glDeleteBuffers(1, &m_ebo);
			m_vao = 0;
			m_vbo = 0;
			m_ebo = 0;
			m_indexCount = 0;
		}
		for (auto& tex : m_textures)
		{
			tex.dispose();
		}
		m_textures.clear();
	}

	static std::vector<float> getAccessorFloats(const json& root, const json& accessor, const std::vector<uint8_t>& binaryData)
	{
		std::vector<float> result;
		unsigned int buffViewInd = accessor.value("bufferView", 0);
		unsigned int count = accessor["count"];
		unsigned int accByteOffset = accessor.value("byteOffset", 0);
		std::string type = accessor["type"];

		const json& bufferView = root["bufferViews"][buffViewInd];
		unsigned int byteOffset = bufferView.value("byteOffset", 0);

		unsigned int numPerVertex = 1;
		if (type == "SCALAR")
			numPerVertex = 1;
		else if (type == "VEC2")
			numPerVertex = 2;
		else if (type == "VEC3")
			numPerVertex = 3;
		else if (type == "VEC4")
			numPerVertex = 4;

		unsigned int start = byteOffset + accByteOffset;
		unsigned int totalFloats = count * numPerVertex;
		result.resize(totalFloats);

		std::memcpy(result.data(), binaryData.data() + start, totalFloats * sizeof(float));
		return result;
	}

	static std::vector<GLuint> getAccessorIndices(const json& root, const json& accessor, const std::vector<uint8_t>& binaryData)
	{
		std::vector<GLuint> indices;
		unsigned int buffViewInd = accessor.value("bufferView", 0);
		unsigned int count = accessor["count"];
		unsigned int accByteOffset = accessor.value("byteOffset", 0);
		unsigned int componentType = accessor["componentType"];

		const json& bufferView = root["bufferViews"][buffViewInd];
		unsigned int byteOffset = bufferView.value("byteOffset", 0);
		unsigned int start = byteOffset + accByteOffset;

		indices.reserve(count);

		if (componentType == 5125) // GL_UNSIGNED_INT
		{
			const uint32_t* ptr = reinterpret_cast<const uint32_t*>(binaryData.data() + start);
			for (unsigned int i = 0; i < count; ++i)
			{
				indices.push_back(ptr[i]);
			}
		}
		else if (componentType == 5123) // GL_UNSIGNED_SHORT
		{
			const uint16_t* ptr = reinterpret_cast<const uint16_t*>(binaryData.data() + start);
			for (unsigned int i = 0; i < count; ++i)
			{
				indices.push_back(ptr[i]);
			}
		}
		else if (componentType == 5122) // GL_SHORT
		{
			const int16_t* ptr = reinterpret_cast<const int16_t*>(binaryData.data() + start);
			for (unsigned int i = 0; i < count; ++i)
			{
				indices.push_back(static_cast<GLuint>(ptr[i]));
			}
		}

		return indices;
	}

	bool Model::loadGLTF(const std::string& gltfPath)
	{
		free();

		std::ifstream gltfFile(gltfPath);
		if (!gltfFile.is_open())
		{
			std::cerr << "Failed to open glTF file: " << gltfPath << std::endl;
			return false;
		}

		json gltfJson;
		try
		{
			gltfFile >> gltfJson;
		}
		catch (const std::exception& e)
		{
			std::cerr << "Failed to parse glTF JSON: " << e.what() << std::endl;
			return false;
		}

		std::string dir = std::filesystem::path(gltfPath).parent_path().string();
		std::string binUri = gltfJson["buffers"][0]["uri"];
		std::string binPath = dir + "/" + binUri;

		std::ifstream binFile(binPath, std::ios::binary | std::ios::ate);
		if (!binFile.is_open())
		{
			std::cerr << "Failed to open glTF binary buffer: " << binPath << std::endl;
			return false;
		}

		std::streamsize binSize = binFile.tellg();
		binFile.seekg(0, std::ios::beg);
		std::vector<uint8_t> binaryData(binSize);
		binFile.read(reinterpret_cast<char*>(binaryData.data()), binSize);

		const auto& primitive = gltfJson["meshes"][0]["primitives"][0];
		unsigned int posAccInd = primitive["attributes"]["POSITION"];
		unsigned int normalAccInd = primitive["attributes"]["NORMAL"];
		unsigned int texAccInd = primitive["attributes"]["TEXCOORD_0"];
		unsigned int indAccInd = primitive["indices"];

		std::vector<float> posData = getAccessorFloats(gltfJson, gltfJson["accessors"][posAccInd], binaryData);
		std::vector<float> normData = getAccessorFloats(gltfJson, gltfJson["accessors"][normalAccInd], binaryData);
		std::vector<float> uvData = getAccessorFloats(gltfJson, gltfJson["accessors"][texAccInd], binaryData);
		std::vector<GLuint> indices = getAccessorIndices(gltfJson, gltfJson["accessors"][indAccInd], binaryData);

		size_t vertexCount = posData.size() / 3;
		std::vector<Vertex> vertices;
		vertices.reserve(vertexCount);

		for (size_t i = 0; i < vertexCount; ++i)
		{
			Vertex v;
			v.position = glm::vec3(posData[i * 3 + 0], posData[i * 3 + 1], posData[i * 3 + 2]);
			v.normal = glm::vec3(normData[i * 3 + 0], normData[i * 3 + 1], normData[i * 3 + 2]);
			v.color = glm::vec3(1.0f);
			if (i * 2 + 1 < uvData.size())
			{
				v.texUV = glm::vec2(uvData[i * 2 + 0], uvData[i * 2 + 1]);
			}
			else
			{
				v.texUV = glm::vec2(0.0f);
			}
			vertices.push_back(v);
		}

		m_indexCount = static_cast<GLsizei>(indices.size());

		glGenVertexArrays(1, &m_vao);
		glGenBuffers(1, &m_vbo);
		glGenBuffers(1, &m_ebo);

		glBindVertexArray(m_vao);

		glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
		glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint), indices.data(), GL_STATIC_DRAW);

		// Attribute 0: position
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, position)));
		glEnableVertexAttribArray(0);

		// Attribute 1: normal
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, normal)));
		glEnableVertexAttribArray(1);

		// Attribute 2: color
		glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, color)));
		glEnableVertexAttribArray(2);

		// Attribute 3: texUV
		glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, texUV)));
		glEnableVertexAttribArray(3);

		glBindVertexArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

		return true;
	}

	void Model::draw(const Shader& shader, const glm::mat4& modelMatrix, int materialIndex) const
	{
		if (m_vao == 0 || m_indexCount == 0)
			return;

		shader.setUniform("u_model", modelMatrix);
		glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(modelMatrix)));
		shader.setUniform("u_normalMatrix", normalMatrix);
		shader.setUniform("u_materialIndex", materialIndex);
		shader.setUniform("u_hasDiffuse", m_textures.empty() ? 0 : 1);

		if (!m_textures.empty())
		{
			m_textures[0].bind(0);
		}

		glBindVertexArray(m_vao);
		glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
		glBindVertexArray(0);
	}
} // namespace renderer
